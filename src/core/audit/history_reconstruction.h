#pragma once

#include "core/audit/event_replayer.h"
#include "core/project_model.h"
#include "sacm_adapter/library_load.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
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

// The reconstructed state at a sequence. `document` is the AUTHORITY — the
// library document the replay produced — and `views` are derived from it, the
// same way `AppState::load_file` derives the live ones. Undo needs the document
// (it becomes the live one, so the bus can serialize it directly and preserve
// content no projection carries); the History Timeline needs the views.
struct ReconstructedState {
    std::unique_ptr<sacm_adapter::LibraryDocument> document;
    ReplayState                                    views;
};

// Reconstruct the state of the project as of (immediately after) the
// transaction with sequence `target_transaction_sequence`. Passing 0 returns
// the initial snapshot state with no events applied. Passing a value larger
// than the latest known sequence returns the latest state (replays
// everything).
//
// `views` are derived exactly as `AppState::load_file` derives them
// (`core::RebuildDerivedViewsFromLibrary`): a tag-carrying package that keeps
// each ArgumentPackage separate, plus the render passes. That is load-bearing
// rather than tidy — an undo that takes the legacy path assigns them over the
// live model and package, which the command bus then SERIALIZES, so anything
// the derivation drops is written to the tracked file. They are deliberately
// not the audit projection (`core::project_library_package`), which may
// collapse packages and carries no vendor TaggedValues; callers needing the
// audit hash take it through `core::library_canonical_hash`, which re-projects
// from the package and is therefore unaffected by this.
//
// When `argument_package_id` (or `argument_package_gid`) is non-empty,
// `views.model` is projected down to elements owned by that `ArgumentPackage`
// so the History Timeline can mirror the GSN canvas's per-package scope.
// `views.package` stays project-wide, and `document` is never scoped — it is
// the whole reconstructed document.
//
// Returns an error string when the audit store is missing, the snapshot
// cannot be loaded, the event log cannot be opened or a single event fails
// to replay.
std::expected<ReconstructedState, std::string> ReconstructAtSequence(
    const AssuranceProject& project,
    std::uint64_t target_transaction_sequence,
    const std::string& argument_package_id = {},
    const std::string& argument_package_gid = {});

} // namespace core::audit
