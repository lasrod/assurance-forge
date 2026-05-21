#pragma once

#include "core/project_model.h"

#include <filesystem>
#include <string>

// High-level orchestrator for the audit / event-store subsystem (design §5,
// §6). Wraps the file-layout, snapshot and event-store primitives behind a
// single idempotent `EnsureAuditStore` entry point that is called from the
// project-open / project-create flows.
namespace core::audit {

struct EnsureAuditStoreResult {
    bool        created = false;       // true if the store was freshly created
    std::string snapshot_id;           // populated on success
    std::string raw_file_hash;
    std::string canonical_model_hash;
};

// Ensure `<project_root>/.af/` contains a valid audit store and at least
// snapshot 0. If the manifest is missing, the SACM file at `<project_root>/<
// sacm_relative_path>` is parsed, hashed, copied into snapshot 0, and the
// manifest plus an empty event log are written. Existing stores are left
// untouched.
//
// Safe to call on every project open; cheap fast-path when the manifest is
// already present and references the same SACM path.
bool EnsureAuditStore(const AssuranceProject& project,
                      const std::filesystem::path& sacm_relative_path,
                      EnsureAuditStoreResult& out_result,
                      std::string& error);

// Result of a reconciliation operation.
struct ReconcileAuditStoreResult {
    // Absolute path to the backup directory the previous audit-store
    // artifacts were moved into. Empty when no backup was needed.
    std::string backup_dir;
    // Identifier of the new initial snapshot ("snapshot_000000").
    std::string snapshot_id;
    std::string canonical_model_hash;
};

// Rebuild the audit store for `project` from the currently-materialized SACM
// file. Used when replay verification (`VerifyProject`) reports divergence
// between the on-disk SACM and the replayed audit log — i.e., mutations
// happened without being recorded as transactions and the log can no longer
// reproduce live state.
//
// Behavior:
//   1) Move the existing `manifest.af.json`, `snapshots/` and `audit/`
//      directory under a timestamped `.af/backup_<utc>/` so prior history is
//      preserved on disk for forensics.
//   2) Re-run `EnsureAuditStore` against the current SACM, which writes a
//      fresh snapshot 0, an empty event log and a new manifest whose
//      `last_known_canonical_model_hash` matches the live model.
//
// On success, the caller is responsible for reopening any `EventStore` or
// `CommandBus` instance that referenced the previous state.
bool ReconcileAuditStore(const AssuranceProject& project,
                         const std::filesystem::path& sacm_relative_path,
                         ReconcileAuditStoreResult& out_result,
                         std::string& error);

} // namespace core::audit
