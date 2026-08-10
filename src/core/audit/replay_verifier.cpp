#include "core/audit/replay_verifier.h"

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_replayer.h"
#include "core/audit/event_store.h"
#include "core/library_package_projection.h"
#include "core/project_file_io.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_parser.h"
#include "legacy_sacm/sacm_serializer.h"
#include "sacm_adapter/library_load.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>

namespace core::audit {

const char* ToString(DivergenceCause cause) {
    switch (cause) {
    case DivergenceCause::None:
        return "None";
    case DivergenceCause::ManifestUnreadable:
        return "ManifestUnreadable";
    case DivergenceCause::SnapshotMissing:
        return "SnapshotMissing";
    case DivergenceCause::EventLogCorrupt:
        return "EventLogCorrupt";
    case DivergenceCause::ReplayFailed:
        return "ReplayFailed";
    case DivergenceCause::ReplayDoesNotMatchOnDisk:
        return "ReplayDoesNotMatchOnDisk";
    case DivergenceCause::OnDiskMissing:
        return "OnDiskMissing";
    case DivergenceCause::ManifestStale:
        return "ManifestStale";
    }
    return "Unknown";
}

ReplayVerificationResult VerifyProject(const AssuranceProject& project) {
    ReplayVerificationResult result;

    if (project.rootPath.empty()) {
        result.success = true;
        result.ran = false;
        result.diagnostics.emplace_back("Project has no root path; verifier skipped.");
        return result;
    }

    const std::filesystem::path manifest_path = ManifestPath(project.rootPath);
    if (!std::filesystem::exists(manifest_path)) {
        result.success = true;
        result.ran = false;
        result.diagnostics.emplace_back("No audit manifest at " + manifest_path.string() + "; verifier skipped.");
        return result;
    }

    AuditManifest manifest;
    {
        std::string err;
        if (!ReadAuditManifest(project.rootPath, manifest, err)) {
            result.success = false;
            result.ran = true;
            result.cause = DivergenceCause::ManifestUnreadable;
            result.diagnostics.emplace_back("Failed to read audit manifest: " + err);
            return result;
        }
    }
    result.manifest_canonical_hash = manifest.last_known_canonical_model_hash;

    // Replay from the trusted root (a promoted baseline when present, else
    // snapshot 0), applying only events after the root's transaction sequence.
    const ReplayRoot replay_root =
        ResolveReplayRoot(project.rootPath, manifest.replay_root_snapshot_id, manifest.initial_snapshot_id);

    // Phase 1b flip: load the trusted-root snapshot as a library document and
    // replay the log forward THROUGH the library (`ReplayToLibrary`). The library
    // is now the replay source of truth; `ReplayFrom` and the legacy models
    // survive only as the convergence oracle's reference (tests/test_library_
    // replay_convergence.cpp).
    const std::filesystem::path snapshot_path = SnapshotSacmPath(project.rootPath, replay_root.snapshot_id);
    sacm_adapter::LoadOutcome snapshot = sacm_adapter::load_document(snapshot_path);
    if (!snapshot.ok || snapshot.document == nullptr) {
        const std::string diagnostics = sacm_adapter::summarize_load_diagnostics(snapshot.diagnostics);
        result.success = false;
        result.ran = true;
        result.cause = DivergenceCause::SnapshotMissing;
        result.diagnostics.emplace_back("Failed to load snapshot through the library: " + snapshot_path.string() +
                                        (diagnostics.empty() ? "" : " (" + diagnostics + ")"));
        return result;
    }
    const sacm::AssuranceCasePackage snapshot_package = core::project_library_package(*snapshot.document);
    // The fallback normalizes DIFFERENTLY from the replayed side below, which
    // hashes through `library_canonical_hash` unconditionally, so a project whose
    // projection cannot be reloaded would report divergence permanently with
    // nothing saying why. It stays a fallback rather than a hard failure -- a
    // verification that cannot hash the snapshot should still compare what it can
    // -- but it no longer engages silently. A duplicate-id projection did exactly
    // this: the snapshot took the fallback while the replayed side failed hard.
    if (std::optional<std::string> snapshot_hash = core::library_canonical_hash(snapshot_package)) {
        result.snapshot_canonical_hash = *snapshot_hash;
    } else {
        result.snapshot_canonical_hash = CanonicalModelHash(snapshot_package);
        result.diagnostics.emplace_back(
            "Snapshot projection could not be reloaded through the library; hashed without that normalization, "
            "which does not match how the replayed side is hashed");
    }

    std::unique_ptr<EventStore> store;
    {
        std::string err;
        store = EventStore::Open(project.rootPath, err);
        if (!store) {
            result.success = false;
            result.ran = true;
            result.cause = DivergenceCause::EventLogCorrupt;
            result.diagnostics.emplace_back("Failed to open event store: " + err);
            return result;
        }
    }

    auto replayed = Replayer::ReplayToLibrary(std::move(snapshot.document),
                                              store->Transactions(),
                                              std::numeric_limits<std::uint64_t>::max(),
                                              replay_root.from_transaction_sequence);
    if (!replayed) {
        result.success = false;
        result.ran = true;
        result.cause = DivergenceCause::ReplayFailed;
        result.diagnostics.emplace_back("Replay failed: " + replayed.error());
        return result;
    }
    // Hash the replayed library the same way the on-disk read and the manifest
    // cache derive theirs (`library_canonical_hash`), so all three converge.
    {
        auto library_hash = core::library_canonical_hash(core::project_library_package(**replayed));
        if (!library_hash) {
            result.success = false;
            result.ran = true;
            result.cause = DivergenceCause::ReplayFailed;
            result.diagnostics.emplace_back("Failed to load replayed SACM through the library for normalization");
            return result;
        }
        result.replayed_canonical_hash = *library_hash;
    }

    // Compare against the materialized SACM on disk, if available.
    const std::filesystem::path on_disk_sacm = project.rootPath / manifest.current_sacm;
    bool on_disk_present = std::filesystem::exists(on_disk_sacm);
    if (on_disk_present) {
        auto disk_hash = core::library_canonical_hash_from_file(on_disk_sacm);
        if (!disk_hash) {
            result.diagnostics.emplace_back("Failed to load on-disk SACM through the library for comparison");
        } else {
            result.on_disk_canonical_hash = *disk_hash;
        }
    }

    result.ran = true;
    result.success = true;
    // Phase 0 — silent manifest rebuild on cache-miss.
    //
    // The manifest is a *cache* of hashes derived from the canonical sources
    // of truth (the snapshot + event log on one side, the materialized SACM
    // on the other). When replay agrees with the on-disk SACM but the
    // cached manifest hash is stale, the previous behavior surfaced a
    // false-positive divergence and pushed the user into the (destructive)
    // Reconcile flow. Here we instead rewrite the manifest silently and
    // continue as if everything matched. We only flag a user-facing
    // divergence when the log ↔ SACM relationship is actually broken.
    const bool replay_matches_disk =
        !result.on_disk_canonical_hash.empty() && result.on_disk_canonical_hash == result.replayed_canonical_hash;
    const bool manifest_stale =
        !result.manifest_canonical_hash.empty() && result.manifest_canonical_hash != result.replayed_canonical_hash;
    if (replay_matches_disk && manifest_stale) {
        result.cause = DivergenceCause::ManifestStale;
        AuditManifest rebuilt = manifest;
        rebuilt.last_known_canonical_model_hash = result.replayed_canonical_hash;
        // Refresh raw file hash too — if the canonical content matches, the
        // raw hash is the one paired with that content. Best effort: if the
        // hash read fails, leave the previous value.
        if (auto raw = Sha256File(on_disk_sacm); raw) {
            rebuilt.last_known_raw_file_hash = *raw;
        }
        // Event store hash and sequence counters reflect the log we just
        // replayed successfully.
        rebuilt.event_store_hash = store->EventStoreHash();
        rebuilt.latest_transaction_sequence = store->LatestTransactionSequence();
        rebuilt.latest_event_sequence = store->LatestEventSequence();
        std::string write_err;
        if (WriteAuditManifest(project.rootPath, rebuilt, write_err)) {
            result.manifest_canonical_hash = rebuilt.last_known_canonical_model_hash;
            result.diagnostics.emplace_back("Manifest hashes were stale relative to replayed state; "
                                            "manifest silently rebuilt from snapshot+log+SACM.");
        } else {
            // Don't fail verification just because the cache refresh
            // failed; surface a diagnostic and keep going.
            result.diagnostics.emplace_back("Manifest hashes were stale and silent rebuild failed: " + write_err);
        }
    } else if (!result.manifest_canonical_hash.empty() &&
               result.manifest_canonical_hash != result.replayed_canonical_hash) {
        result.success = false;
        if (result.cause == DivergenceCause::None)
            result.cause = DivergenceCause::ReplayDoesNotMatchOnDisk;
        result.diagnostics.emplace_back(
            "Replayed canonical hash does not match manifest.last_known_canonical_model_hash (replay=" +
            result.replayed_canonical_hash + ", manifest=" + result.manifest_canonical_hash + ").");
    }
    if (!result.on_disk_canonical_hash.empty() && result.on_disk_canonical_hash != result.replayed_canonical_hash) {
        result.success = false;
        result.cause = DivergenceCause::ReplayDoesNotMatchOnDisk;
        result.diagnostics.emplace_back("Replayed canonical hash does not match on-disk SACM canonical hash (replay=" +
                                        result.replayed_canonical_hash + ", on_disk=" + result.on_disk_canonical_hash +
                                        ").");
    }
    if (!on_disk_present) {
        result.success = false;
        result.cause = DivergenceCause::OnDiskMissing;
        result.diagnostics.emplace_back("On-disk SACM file referenced by the manifest is missing: " +
                                        on_disk_sacm.string());
    }
    return result;
}

} // namespace core::audit
