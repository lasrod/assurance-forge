#pragma once

#include "app/app_runtime_state.h"
#include "core/commands/command_bus.h"

#include <cstdint>
#include <string>

// Bridges the app-actions layer to `core::commands::CommandBus`. Every
// model mutation triggered by a menu action / button click should flow
// through `DispatchAuditedCommand` so that it is auto-saved and recorded
// in the audit timeline. This mirrors `ElementEditController`'s direct
// use of the bus but is callable from non-controller action classes.
namespace app::commands {

// Applies a pending library-primary re-derive, if one is due, and clears the
// flag. Call it at a FRAME BOUNDARY, never mid-dispatch.
//
// `RebuildDerivedViewsFromLibrary` replaces `loaded_case` and `sacm_package`
// wholesale, which frees containers a panel may still be rendering from -- the
// create-a-Claim crash. Dispatch therefore only ever RAISES the flag; the
// runtime drains it at the top of the next frame, before any panel renders.
// Tests that need the derived views refreshed call this to stand in for that
// frame.
void ApplyPendingLibraryRederive(AppRuntimeState& state);

struct DispatchOutcome {
    bool success = false;
    std::string error;
    // True only when the accepted SACM file contains the command result. Audit
    // intent may already be committed while autosave failed, so promotion must
    // not delete its recovery workspace based on `success` alone.
    bool sacm_written = false;
    // The audit transaction this command recorded, and the sequence a later undo
    // targets. Zero when there was no bus to record one. Draft promotion keys its
    // pre-promotion snapshot on this, which is what lets undo tell a promotion
    // apart from an ordinary proposal application.
    std::uint64_t transaction_sequence = 0;
};

// Run `command` through the project's audit bus. Returns failure if no
// bus or no loaded model is available. Emits `AutosaveFailedEvent` to
// surface (or clear) the autosave banner; does NOT emit DocumentDirty /
// TreeDirty / StatusMessage events — callers decide whether to do so.
//
// `author` is who the audit log records. Empty means the person at the
// keyboard, which is what an ordinary menu action wants. Accepting an AI
// client's change set passes the client, because "who changed this" is the
// question the audit log of a safety argument exists to answer, and an edge
// case where the answer is "an agent proposed it and a person approved it" is
// the one it must not silently record as `system`.
// `draft_promotion` is recorded on the audit transaction and is empty for every
// command except a draft promotion, whose provenance the log has to carry
// because the draft it came from is consumed by the act of accepting it.
DispatchOutcome DispatchAuditedCommand(AppRuntimeState& state,
                                       core::commands::ICommand& command,
                                       const std::string& author = {},
                                       const core::audit::DraftPromotionRecord& draft_promotion = {});

} // namespace app::commands
