#include "core/audit/audit_store.h"

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/event_store.h"
#include "core/time_utils.h"

namespace core::audit {

bool EnsureAuditStore(const AssuranceProject& project,
                      const std::filesystem::path& sacm_relative_path,
                      EnsureAuditStoreResult& out_result,
                      std::string& error) {
    out_result = EnsureAuditStoreResult{};

    if (project.rootPath.empty()) {
        error = "Project has no root path; cannot initialize audit store";
        return false;
    }
    if (sacm_relative_path.empty()) {
        error = "No SACM file path supplied for audit store initialization";
        return false;
    }

    const std::filesystem::path& root = project.rootPath;
    const std::filesystem::path manifest_path = ManifestPath(root);

    std::error_code ec;
    std::filesystem::create_directories(AfDir(root), ec);
    if (ec) {
        error = "Could not create .af directory: " + ec.message();
        return false;
    }

    if (std::filesystem::exists(manifest_path, ec)) {
        // Fast path: store already initialized. Load the manifest so callers
        // get a populated result without re-hashing the SACM file.
        AuditManifest existing;
        if (!ReadAuditManifest(root, existing, error))
            return false;
        out_result.created = false;
        out_result.snapshot_id = existing.initial_snapshot_id;
        out_result.raw_file_hash = existing.last_known_raw_file_hash;
        out_result.canonical_model_hash = existing.last_known_canonical_model_hash;
        return true;
    }

    const std::filesystem::path source_sacm = root / sacm_relative_path;
    if (!std::filesystem::exists(source_sacm, ec)) {
        error = "SACM file not found for audit store initialization: " + source_sacm.string();
        return false;
    }

    SnapshotMetadata snapshot;
    if (!CreateInitialSnapshot(root, source_sacm, project.lastOpenedWith, snapshot, error))
        return false;

    // Initialize an empty event log so subsequent code paths can `Open` it
    // without conditional create logic.
    auto event_store = EventStore::Open(root, error);
    if (!event_store)
        return false;

    AuditManifest manifest;
    manifest.manifest_schema_version = kManifestSchemaVersion;
    manifest.project_id = project.id;
    manifest.created_at = NowUtcString();
    manifest.created_by = project.lastOpenedWith.empty() ? std::string("system") : project.lastOpenedWith;
    manifest.current_sacm = sacm_relative_path.generic_string();
    manifest.initial_snapshot_id = snapshot.snapshot_id;
    manifest.latest_transaction_sequence = 0;
    manifest.latest_event_sequence = 0;
    manifest.last_known_raw_file_hash = snapshot.raw_file_hash;
    manifest.last_known_canonical_model_hash = snapshot.canonical_model_hash;
    manifest.event_store_hash = event_store->EventStoreHash();

    if (!WriteAuditManifest(root, manifest, error))
        return false;

    out_result.created = true;
    out_result.snapshot_id = snapshot.snapshot_id;
    out_result.raw_file_hash = snapshot.raw_file_hash;
    out_result.canonical_model_hash = snapshot.canonical_model_hash;
    return true;
}

} // namespace core::audit
