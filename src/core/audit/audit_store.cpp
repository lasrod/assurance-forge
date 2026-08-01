#include "core/audit/audit_store.h"

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/event_store.h"
#include "core/time_utils.h"

#include <string>
#include <system_error>

namespace core::audit {

namespace {

// Replace ':' with '-' so the UTC ISO timestamp is a valid path component on
// Windows and easy to copy/paste elsewhere.
std::string FilenameSafeNowUtc() {
    std::string s = NowUtcString();
    for (char& c : s) {
        if (c == ':')
            c = '-';
    }
    return s;
}

// Move `source` (a file or directory) into `target`, falling back to a
// recursive copy + remove when `rename` is not supported across the
// underlying filesystem boundary. Missing source is a no-op.
bool MoveIntoBackup(const std::filesystem::path& source, const std::filesystem::path& target, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(source, ec))
        return true;

    std::filesystem::rename(source, target, ec);
    if (!ec)
        return true;

    std::error_code copy_ec;
    std::filesystem::copy(source,
                          target,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                          copy_ec);
    if (copy_ec) {
        error = "Could not back up " + source.string() + ": " + copy_ec.message();
        return false;
    }
    std::error_code rm_ec;
    std::filesystem::remove_all(source, rm_ec);
    if (rm_ec) {
        error = "Could not remove " + source.string() + " after backup: " + rm_ec.message();
        return false;
    }
    return true;
}

} // namespace

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

bool ReconcileAuditStore(const AssuranceProject& project,
                         const std::filesystem::path& sacm_relative_path,
                         ReconcileAuditStoreResult& out_result,
                         std::string& error) {
    out_result = ReconcileAuditStoreResult{};

    if (project.rootPath.empty()) {
        error = "Project has no root path; cannot reconcile audit store";
        return false;
    }
    if (sacm_relative_path.empty()) {
        error = "No SACM file path supplied for audit store reconciliation";
        return false;
    }

    const std::filesystem::path& root = project.rootPath;
    std::error_code ec;
    const std::filesystem::path source_sacm = root / sacm_relative_path;
    if (!std::filesystem::exists(source_sacm, ec)) {
        error = "SACM file not found for audit store reconciliation: " + source_sacm.string();
        return false;
    }

    std::filesystem::create_directories(AfDir(root), ec);
    if (ec) {
        error = "Could not create .af directory: " + ec.message();
        return false;
    }

    // Build a unique backup directory under `.af/backup_<ts>[_n]/`.
    const std::string ts = FilenameSafeNowUtc();
    std::filesystem::path backup = AfDir(root) / ("backup_" + ts);
    int attempt = 0;
    while (std::filesystem::exists(backup, ec)) {
        ++attempt;
        backup = AfDir(root) / ("backup_" + ts + "_" + std::to_string(attempt));
    }
    std::filesystem::create_directories(backup, ec);
    if (ec) {
        error = "Could not create backup directory: " + ec.message();
        return false;
    }

    // Move prior manifest, snapshots and event log into the backup. Each call
    // tolerates a missing source, so partially-initialized stores still
    // reconcile cleanly.
    if (!MoveIntoBackup(ManifestPath(root), backup / kManifestFileName, error))
        return false;
    if (!MoveIntoBackup(SnapshotsDir(root), backup / kSnapshotsDirName, error))
        return false;
    if (!MoveIntoBackup(AuditDir(root), backup / kAuditDirName, error))
        return false;

    EnsureAuditStoreResult ensure;
    if (!EnsureAuditStore(project, sacm_relative_path, ensure, error))
        return false;

    out_result.backup_dir = backup.string();
    out_result.snapshot_id = ensure.snapshot_id;
    out_result.canonical_model_hash = ensure.canonical_model_hash;
    return true;
}

} // namespace core::audit
