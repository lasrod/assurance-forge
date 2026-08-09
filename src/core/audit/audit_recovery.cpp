#include "core/audit/audit_recovery.h"

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
#include "core/sha256.h"

#include <cstdint>
#include <filesystem>
#include <limits>

namespace core::audit {

bool RestoreSacmFromAudit(const AssuranceProject& project,
                          const std::filesystem::path& sacm_relative_path,
                          const std::string& author,
                          RestoreSacmFromAuditResult& out_result,
                          std::string& error) {
    out_result = RestoreSacmFromAuditResult{};

    if (project.rootPath.empty()) {
        error = "Project has no root path; cannot restore SACM from audit";
        return false;
    }
    if (sacm_relative_path.empty()) {
        error = "No SACM file path supplied for restore";
        return false;
    }

    // Load manifest so we know which snapshot to replay from.
    AuditManifest manifest;
    if (!ReadAuditManifest(project.rootPath, manifest, error))
        return false;

    // Replay from the trusted root (a promoted baseline when present, else
    // snapshot 0), applying only events after the root's transaction sequence.
    const ReplayRoot replay_root =
        ResolveReplayRoot(project.rootPath, manifest.replay_root_snapshot_id, manifest.initial_snapshot_id);

    // Phase 1b flip: load the trusted-root snapshot as a library document and
    // replay forward through the library (`ReplayToLibrary`).
    const std::filesystem::path snapshot_path = SnapshotSacmPath(project.rootPath, replay_root.snapshot_id);
    sacm_adapter::LoadOutcome snapshot = sacm_adapter::load_document(snapshot_path);
    if (!snapshot.ok || snapshot.document == nullptr) {
        const std::string diagnostics = sacm_adapter::summarize_load_diagnostics(snapshot.diagnostics);
        error = "Failed to load snapshot through the library: " + snapshot_path.string() +
                (diagnostics.empty() ? "" : " (" + diagnostics + ")");
        return false;
    }

    auto store = EventStore::Open(project.rootPath, error);
    if (!store)
        return false;

    auto replayed = Replayer::ReplayToLibrary(std::move(snapshot.document),
                                              store->Transactions(),
                                              std::numeric_limits<std::uint64_t>::max(),
                                              replay_root.from_transaction_sequence);
    if (!replayed) {
        error = "Replay failed: " + replayed.error();
        return false;
    }

    // Capture the on-disk hash BEFORE rewriting, so the audit event records
    // what state was overwritten. Best effort: missing/unparsable files
    // leave this empty.
    const std::filesystem::path sacm_abs = project.rootPath / sacm_relative_path;
    std::string pre_restore_canonical;
    if (std::filesystem::exists(sacm_abs)) {
        if (auto library_hash = core::library_canonical_hash_from_file(sacm_abs))
            pre_restore_canonical = *library_hash;
    }

    // Serialize the replayed document and write it atomically. The canonical
    // hash is taken from a reparse of the serialized bytes for round-trip
    // stability (mirrors VerifyProject). Phase 9 Stage 6: write library SACM XMI
    // so the restored file matches the live save format. Phase 9 Stage 7: the
    // replay target IS a library document, so serialize it directly instead of
    // round-tripping the package projected from it -- the same save the live
    // path uses, so a restored file and an autosaved file describe the same
    // state the same way. Whatever unknown content the trusted-root snapshot
    // carried into the replay therefore survives the restore too.
    //
    // The projection fallback keeps recovery robust if the library round-trip
    // fails: the restored file is then a package projection (legacy XMI, or
    // legacy XML if even that fails), which the audit still reads through the
    // library and verifies consistently; the format difference is benign.
    std::string xml;
    {
        sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(**replayed);
        if (saved.ok) {
            xml = std::move(saved.xml);
        } else {
            const sacm::AssuranceCasePackage replayed_package = core::project_library_package(**replayed);
            xml = core::library_xmi_from_package(replayed_package).value_or(sacm::serialize_sacm(replayed_package));
            // Report the degraded path: the projection cannot carry the unknown /
            // vendor-specific content the replayed document held, so a caller must
            // not present this as a fully faithful restore.
            out_result.lossy_fallback_warning =
                "Could not serialize the replayed SACM library document; restored a projection "
                "instead, which does not preserve unknown or vendor-specific content.";
        }
    }
    auto library_hash = core::library_canonical_hash_from_xml(xml);
    if (!library_hash) {
        error = "Failed to load restored SACM through the library for hashing";
        return false;
    }
    const std::string restored_canonical = *library_hash;
    const std::string restored_raw = Sha256::HexDigest(xml);

    auto write = WriteTextFileAtomic(sacm_abs, xml);
    if (!write) {
        error = "Failed to atomically write restored SACM: " + write.error();
        return false;
    }

    // Append a SacmRestoredFromAudit transaction documenting the recovery.
    // This is a normal hash-chained transaction — it keeps the audit log
    // continuous and lets future replays observe that a restore occurred.
    AuditEvent event;
    event.event_type = "SacmRestoredFromAudit";
    event.payload = {
        {"sacm_path", sacm_relative_path.generic_string()},
        {"pre_restore_canonical_hash", pre_restore_canonical},
        {"restored_canonical_hash", restored_canonical},
        {"restored_raw_hash", restored_raw},
    };

    AuditTransaction tx;
    tx.command_name = "RestoreSacmFromAudit";
    tx.author = author.empty() ? std::string("system") : author;
    tx.events.push_back(std::move(event));

    std::string append_error;
    if (!store->Append(tx, append_error)) {
        error = "Failed to append SacmRestoredFromAudit transaction: " + append_error;
        return false;
    }

    // Refresh manifest cache so the next verifier run sees the consistent
    // state without needing a silent rebuild.
    manifest.latest_transaction_sequence = store->LatestTransactionSequence();
    manifest.latest_event_sequence = store->LatestEventSequence();
    manifest.event_store_hash = store->EventStoreHash();
    manifest.last_known_raw_file_hash = restored_raw;
    manifest.last_known_canonical_model_hash = restored_canonical;
    std::string manifest_err;
    if (!WriteAuditManifest(project.rootPath, manifest, manifest_err)) {
        // Non-fatal: the next open will rebuild manifest from log+SACM.
        error = "Restore completed but manifest update failed: " + manifest_err;
        // Still populate out_result so the caller can surface partial info.
    }

    out_result.sacm_path = sacm_abs.string();
    out_result.restored_canonical_hash = restored_canonical;
    out_result.restored_raw_hash = restored_raw;
    out_result.pre_restore_on_disk_canonical_hash = pre_restore_canonical;
    out_result.transaction_sequence = tx.transaction_sequence;
    return manifest_err.empty();
}

} // namespace core::audit
