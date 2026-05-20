#pragma once

#include "core/audit/event_replayer.h"
#include "core/project_model.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

// History reconstruction helpers used by the History Timeline workspace
// (design §16.2). Given a project (with an initialized audit store) and a
// target transaction sequence, returns the reconstructed model state by
// replaying snapshot 0 + all transactions up to the requested sequence.
//
// This is pure logic that lives in `core` so UI code can call it without
// pulling in the GSN canvas dependency tree. UI orchestration / caching
// strategy belongs in the `app` layer.
namespace core::audit {

// Reconstruct the state of the project as of (immediately after) the
// transaction with sequence `target_transaction_sequence`. Passing 0 returns
// the initial snapshot state with no events applied. Passing a value larger
// than the latest known sequence returns the latest state (replays
// everything).
//
// Returns an error string when the audit store is missing, the snapshot
// cannot be loaded, the event log cannot be opened or a single event fails
// to replay.
std::expected<ReplayState, std::string> ReconstructAtSequence(const AssuranceProject& project,
                                                              std::uint64_t target_transaction_sequence);

} // namespace core::audit
