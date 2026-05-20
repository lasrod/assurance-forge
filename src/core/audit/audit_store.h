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

} // namespace core::audit
