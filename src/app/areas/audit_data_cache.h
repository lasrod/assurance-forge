#pragma once

#include "core/audit/audit_baseline.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_transaction.h"

#include <filesystem>
#include <string>
#include <vector>

// Process-wide cached read of audit-store derived data. The Workbench /
// Inspector / History panels all call `EventStore::Open` (full log re-read
// + SHA-256 chain verification), `ListBaselines` (manifest read + sidecar
// reads), and a `directory_iterator` walk over `.af/snapshots/` on every
// frame. With even a few hundred transactions this dominates the frame
// budget and trashes the disk cache.
//
// These helpers stash the parsed result and invalidate it from a cheap
// `stat` (file/directory mtime + size). When nothing changed since the last
// frame, the call is just a stat — no parsing, no SHA, no allocations.
namespace app::areas {

// Cached list of audit transactions for `project_root`. Re-reads the event
// log only when its mtime or size has changed. Returns an empty vector and
// fills `error_out` if the store cannot be opened; on transient errors the
// previously-cached vector is returned so the UI does not flicker.
const std::vector<core::audit::AuditTransaction>& GetCachedTransactions(const std::filesystem::path& project_root,
                                                                        std::string& error_out);

// Cached list of baselines for `project_root`. Invalidated by changes to
// either the audit manifest (where the id list lives) or the baselines
// directory (where sidecar files live).
const std::vector<core::audit::BaselineMetadata>& GetCachedBaselines(const std::filesystem::path& project_root,
                                                                     std::vector<std::string>* warnings_out = nullptr);

// Cached enumeration of snapshot metadata for `project_root`. Invalidated
// by changes to the `.af/snapshots/` directory mtime.
const std::vector<core::audit::SnapshotMetadata>& GetCachedSnapshots(const std::filesystem::path& project_root);

// Clear all caches. Call this when a project is closed so a subsequent
// open of a different project starts fresh.
void ClearAuditDataCache();

} // namespace app::areas
