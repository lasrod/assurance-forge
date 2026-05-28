#pragma once

#include "core/project_model.h"

#include <filesystem>
#include <string>

// Non-destructive remediation helpers (design §13 Phase 5).
//
// These helpers let the application recover from divergence between the
// audit log and the on-disk SACM without discarding history. They keep the
// hash chain intact by appending a normal transaction documenting the
// remediation rather than starting a new chain.
namespace core::audit {

struct RestoreSacmFromAuditResult {
    // Path of the SACM file that was rewritten (absolute).
    std::string sacm_path;
    // Canonical model hash of the restored on-disk SACM (post-write).
    std::string restored_canonical_hash;
    // sha256 of the restored on-disk SACM bytes (post-write).
    std::string restored_raw_hash;
    // Canonical model hash of the on-disk SACM the user had before the
    // restore (may be empty if the file did not exist or could not be
    // parsed).
    std::string pre_restore_on_disk_canonical_hash;
    // Sequence number of the appended SacmRestoredFromAudit transaction.
    std::uint64_t transaction_sequence = 0;
};

// Rebuild the on-disk SACM file at `<project_root>/<sacm_relative_path>` by
// replaying the audit log on top of the initial snapshot, atomically writing
// the result, and appending a `SacmRestoredFromAudit` transaction to the log.
//
// Preconditions:
//   - The project has an initialized audit store (manifest present).
//   - No `CommandBus` is currently holding the event log open. Callers must
//     tear down the bus before invoking and re-open it afterwards (same
//     constraint as `ReconcileAuditStore`).
//
// On success, the on-disk SACM matches the replayed model and the audit
// manifest hashes are refreshed.
bool RestoreSacmFromAudit(const AssuranceProject& project,
                          const std::filesystem::path& sacm_relative_path,
                          const std::string& author,
                          RestoreSacmFromAuditResult& out_result,
                          std::string& error);

} // namespace core::audit
