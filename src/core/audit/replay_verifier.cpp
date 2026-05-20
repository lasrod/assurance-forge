#include "core/audit/replay_verifier.h"

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_replayer.h"
#include "core/audit/event_store.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <cstdint>
#include <filesystem>
#include <limits>

namespace core::audit {

namespace {

bool LoadSnapshot(const std::filesystem::path& project_root,
                  const std::string& snapshot_id,
                  parser::AssuranceCase& out_model,
                  sacm::AssuranceCasePackage& out_package,
                  std::string& out_error) {
    const std::filesystem::path sacm_path = SnapshotSacmPath(project_root, snapshot_id);
    auto pkg = sacm::parse_sacm(sacm_path.string());
    if (!pkg) {
        out_error = "Failed to parse snapshot SACM at " + sacm_path.string() + ": " + pkg.error();
        return false;
    }
    auto ac = parser::parse_sacm_xml(sacm_path.string());
    if (!ac) {
        out_error = "Failed to parse snapshot parser-model at " + sacm_path.string() + ": " + ac.error();
        return false;
    }
    out_package = std::move(*pkg);
    out_model = std::move(*ac);
    return true;
}

} // namespace

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
        result.diagnostics.emplace_back("No audit manifest at " + manifest_path.string() +
                                        "; verifier skipped.");
        return result;
    }

    AuditManifest manifest;
    {
        std::string err;
        if (!ReadAuditManifest(project.rootPath, manifest, err)) {
            result.success = false;
            result.ran = true;
            result.diagnostics.emplace_back("Failed to read audit manifest: " + err);
            return result;
        }
    }
    result.manifest_canonical_hash = manifest.last_known_canonical_model_hash;

    parser::AssuranceCase snapshot_model;
    sacm::AssuranceCasePackage snapshot_package;
    {
        std::string err;
        if (!LoadSnapshot(project.rootPath, manifest.initial_snapshot_id, snapshot_model, snapshot_package,
                          err)) {
            result.success = false;
            result.ran = true;
            result.diagnostics.emplace_back(err);
            return result;
        }
    }
    result.snapshot_canonical_hash = CanonicalModelHash(snapshot_package);

    std::unique_ptr<EventStore> store;
    {
        std::string err;
        store = EventStore::Open(project.rootPath, err);
        if (!store) {
            result.success = false;
            result.ran = true;
            result.diagnostics.emplace_back("Failed to open event store: " + err);
            return result;
        }
    }

    auto replayed = Replayer::ReplayFrom(snapshot_model, snapshot_package, store->Transactions(),
                                         std::numeric_limits<std::uint64_t>::max());
    if (!replayed) {
        result.success = false;
        result.ran = true;
        result.diagnostics.emplace_back("Replay failed: " + replayed.error());
        return result;
    }
    // CanonicalModelHash is currently not invariant under serialize/reparse
    // (some transient parser-side fields are dropped on round-trip). To make
    // verifier comparisons meaningful against the on-disk SACM and against
    // the manifest hash (which itself reflects an in-memory mutation
    // post-write), we normalize the replayed package by serializing it and
    // re-parsing. The resulting hash is what a user would get after closing
    // and reopening the project.
    {
        const std::string xml = sacm::serialize_sacm(replayed->package);
        auto reparsed = sacm::parse_sacm_string(xml);
        if (!reparsed) {
            result.success = false;
            result.ran = true;
            result.diagnostics.emplace_back("Failed to reparse replayed SACM for normalization: " +
                                            reparsed.error());
            return result;
        }
        result.replayed_canonical_hash = CanonicalModelHash(*reparsed);
    }

    // Compare against the materialized SACM on disk, if available.
    const std::filesystem::path on_disk_sacm = project.rootPath / manifest.current_sacm;
    if (std::filesystem::exists(on_disk_sacm)) {
        auto disk_pkg = sacm::parse_sacm(on_disk_sacm.string());
        if (!disk_pkg) {
            result.diagnostics.emplace_back("Failed to parse on-disk SACM for comparison: " +
                                            disk_pkg.error());
        } else {
            result.on_disk_canonical_hash = CanonicalModelHash(*disk_pkg);
        }
    }

    result.ran = true;
    result.success = true;
    if (!result.manifest_canonical_hash.empty() &&
        result.manifest_canonical_hash != result.replayed_canonical_hash) {
        result.success = false;
        result.diagnostics.emplace_back(
            "Replayed canonical hash does not match manifest.last_known_canonical_model_hash (replay=" +
            result.replayed_canonical_hash + ", manifest=" + result.manifest_canonical_hash + ").");
    }
    if (!result.on_disk_canonical_hash.empty() &&
        result.on_disk_canonical_hash != result.replayed_canonical_hash) {
        result.success = false;
        result.diagnostics.emplace_back(
            "Replayed canonical hash does not match on-disk SACM canonical hash (replay=" +
            result.replayed_canonical_hash + ", on_disk=" + result.on_disk_canonical_hash + ").");
    }
    return result;
}

} // namespace core::audit
