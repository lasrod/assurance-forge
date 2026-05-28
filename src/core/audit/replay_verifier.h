#pragma once

#include "core/project_model.h"

#include <string>
#include <vector>

// Replay verifier (design §13). For a project that has an initialized audit
// store, the verifier:
//   1) loads the initial snapshot (snapshot_000000) from disk,
//   2) replays every recorded transaction on top of it,
//   3) compares the canonical model hash of the replay against (a) the
//      canonical hash of the currently-materialized SACM file and (b) the
//      `last_known_canonical_model_hash` recorded in the audit manifest.
//
// Verification failure is non-fatal at the application level — the caller
// decides how to surface diagnostics (warning banner, debug assert, etc.).
namespace core::audit {

// Structured classification of why a verification did not produce a fully
// consistent state. Used by the remediation modal to pre-select a default
// remedy and explain the situation in user-facing language. `None` is the
// success case; `ManifestStale` is reported when the manifest cache was
// silently rebuilt (success=true is still set) so that diagnostics consumers
// can surface a soft notice without prompting the user.
enum class DivergenceCause {
    None = 0,
    ManifestUnreadable,
    SnapshotMissing,
    EventLogCorrupt,
    ReplayFailed,
    ReplayDoesNotMatchOnDisk,
    OnDiskMissing,
    ManifestStale,
};

const char* ToString(DivergenceCause cause);

struct ReplayVerificationResult {
    bool                     success = false;
    bool                     ran = false;
    DivergenceCause          cause = DivergenceCause::None;
    std::string              snapshot_canonical_hash;
    std::string              replayed_canonical_hash;
    std::string              on_disk_canonical_hash;
    std::string              manifest_canonical_hash;
    std::vector<std::string> diagnostics;
};

// Run the verifier for `project`. If the project has no audit store yet
// (e.g., legacy SACM opened outside any project), returns `{success=true,
// ran=false}` with an explanatory diagnostic and no error condition.
ReplayVerificationResult VerifyProject(const AssuranceProject& project);

} // namespace core::audit
