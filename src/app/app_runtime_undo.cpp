#include "app/app_runtime.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/areas/audit_data_cache.h"
#include "app/commands/dispatch.h"
#include "core/audit/audit_transaction.h"
#include "core/audit/history_reconstruction.h"
#include "core/audit/undo_boundary.h"
#include "core/audit/undo_resolver.h"
#include "core/commands/undo_command.h"
#include "core/drafts/draft_persistence.h"
#include "core/drafts/draft_workspace.h"
#include "sacm_adapter/case_projection.h"

#include <string>

namespace app {

namespace {

// Look up the active argument-package canvas tab and decide if it is
// scrubbed into the history preview. Undo is blocked while previewing
// because the canvas the user is looking at does not reflect the live
// model. Mirrors the dispatch-layer guard in `app::commands::dispatch`.
bool ActiveCanvasInHistoricalPreview(const AppRuntimeState& state) {
    const std::string& active = state.workbench.active_argument_package_canvas_key;
    if (active.empty())
        return false;
    for (const auto& tab : state.workbench.argument_package_canvas_tabs) {
        if (tab.key == active) {
            return tab.timeline.preview_sequence.has_value();
        }
    }
    return false;
}

} // namespace

bool AppRuntime::CanUndo() const {
    const AppRuntimeState& state = *impl_;
    if (!state.command_bus)
        return false;
    if (!state.app_state.current_project.has_value())
        return false;
    if (!state.app_state.loaded_case.has_value())
        return false;
    if (!state.app_state.has_projected_package())
        return false;
    if (ActiveCanvasInHistoricalPreview(state))
        return false;

    const auto& transactions = state.command_bus->Store().Transactions();
    const auto target = core::audit::FindUndoTarget(transactions);
    if (!target.has_target)
        return false;

    const auto& project = state.app_state.current_project.value();
    const auto& snapshots = areas::GetCachedSnapshots(project.rootPath);
    const auto& baselines = areas::GetCachedBaselines(project.rootPath);
    const auto boundary = core::audit::FindUndoBoundary(snapshots, baselines, target.target_sequence);
    return core::audit::CanUndo(target.target_sequence, boundary);
}

bool AppRuntime::Undo() {
    AppRuntimeState& state = *impl_;

    if (!state.command_bus) {
        state.app_state.status_message = "Undo unavailable: no project audit bus.";
        return false;
    }
    if (!state.app_state.current_project.has_value() || !state.app_state.loaded_case.has_value() ||
        !state.app_state.has_projected_package()) {
        state.app_state.status_message = "Undo unavailable: no project loaded.";
        return false;
    }
    if (ActiveCanvasInHistoricalPreview(state)) {
        state.app_state.status_message = "Cannot undo while viewing history. Return to Latest to make changes.";
        return false;
    }

    const auto& transactions = state.command_bus->Store().Transactions();
    const auto target = core::audit::FindUndoTarget(transactions);
    if (!target.has_target) {
        state.app_state.status_message = "Nothing to undo.";
        return false;
    }

    const auto& project = state.app_state.current_project.value();
    const auto& snapshots = areas::GetCachedSnapshots(project.rootPath);
    const auto& baselines = areas::GetCachedBaselines(project.rootPath);
    const auto boundary = core::audit::FindUndoBoundary(snapshots, baselines, target.target_sequence);
    if (!core::audit::CanUndo(target.target_sequence, boundary)) {
        state.app_state.status_message = "Reached snapshot or baseline — restore from history to go further back.";
        return false;
    }

    // A draft promotion is one transaction on this stack, but the draft it
    // consumed lives nowhere in the audit log. Undoing it therefore has to put
    // the groups back as well, or the change leaves the accepted argument while
    // already having left the draft -- and when the promotion took the last
    // group, the workspace was deleted with it.
    //
    // Loaded before anything is mutated, so a snapshot that cannot be read
    // refuses the undo instead of discovering the problem once the accepted model
    // has already moved. Fail-closed is the right default here: the alternative
    // silently destroys unaccepted work whose only copy this is.
    //
    // Deliberately not gated on an existence check first. `std::filesystem::exists`
    // answers false both for "no snapshot" and for a path it could not
    // interrogate, so gating on it would send a transaction that *is* a promotion
    // down the ordinary path on any IO error -- losing the draft in exactly the
    // case the check was added to protect. Only the loader distinguishes "not a
    // promotion" (false, no error) from "cannot tell" (false, error).
    core::drafts::DraftWorkspace promotion_snapshot;
    std::string snapshot_error;
    const bool undo_restores_draft =
        state.draft_workspace.LoadPromotionSnapshot(target.target_sequence, promotion_snapshot, snapshot_error);
    if (!undo_restores_draft && !snapshot_error.empty()) {
        state.app_state.status_message =
            "Cannot undo this acceptance: the draft it came from could not be read (" + snapshot_error + "). Remove " +
            core::drafts::DraftPromotionSnapshotPath(project.rootPath, target.target_sequence).generic_string() +
            " to undo without restoring it.";
        return false;
    }
    if (undo_restores_draft &&
        !core::drafts::WorkspaceTargetsArgumentFile(promotion_snapshot, state.draft_workspace.argument_file())) {
        state.app_state.status_message = "Cannot undo this acceptance here: it belongs to " +
                                         promotion_snapshot.argument_file.filename().generic_string() +
                                         ". Open that argument and undo there, so its draft is restored with it.";
        return false;
    }

    // Reconstruct the prior state (transaction immediately before the target).
    // ReconstructAtSequence honors existing Undo markers, so chained undos
    // and redos compose correctly without special-casing here.
    auto prior = core::audit::ReconstructAtSequence(project, target.target_sequence - 1);
    if (!prior.has_value()) {
        state.app_state.status_message = "Undo failed: " + prior.error();
        return false;
    }

    // The reconstructed DOCUMENT is handed over too, so the undo is
    // library-primary: the prior document becomes the live one and the bus
    // serializes it, preserving the unknown/foreign content no projection can
    // carry. The views come along for the legacy path (a context with no
    // library document).
    //
    // Which of the two paths runs decides where the restored accepted model can
    // be read from afterwards, so it is settled here while both parts are still
    // in hand. Library-primary deliberately leaves `loaded_case` stale until the
    // next frame boundary; hashing that stale view would rebase the draft onto a
    // model the argument no longer has.
    const bool library_primary_undo = state.app_state.library_document != nullptr && prior.value().document != nullptr;
    core::commands::UndoLastTransactionCommand cmd(target.target_sequence,
                                                   target.target_command_name,
                                                   std::move(prior.value().views.model),
                                                   std::move(prior.value().views.package),
                                                   std::move(prior.value().document));

    const auto outcome = commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        state.app_state.status_message = "Undo failed: " + outcome.error;
        return false;
    }

    std::string draft_note;
    if (undo_restores_draft) {
        const parser::AssuranceCase restored_accepted =
            library_primary_undo ? sacm_adapter::project_case(*state.app_state.library_document)
                                 : state.app_state.loaded_case.value();
        std::string restore_error;
        if (state.draft_workspace.RestorePromotionSnapshot(promotion_snapshot, restored_accepted, restore_error)) {
            // Consumed. Keeping it would offer to restore the same groups a second
            // time if this transaction were ever undone again.
            std::string discard_error;
            state.draft_workspace.DeletePromotionSnapshot(target.target_sequence, discard_error);
        } else {
            // The accepted model has already moved, so this is not a failed undo.
            // Say what was and was not done rather than reporting a clean one: the
            // snapshot file is still there and is still the only copy of that work.
            draft_note = " — but the draft it came from was not restored: " + restore_error;
        }
    }

    // The model has been wholesale-replaced. Rebuild derived views (tree,
    // canvas, registers) on the next frame and drop any selection that may
    // now reference a removed element. Mirrors the post-dispatch fan-out
    // performed by ElementEditController for ordinary mutations.
    state.events.Emit(TreeDirtyEvent{});
    state.events.Emit(SelectionChangedEvent{});
    state.events.Emit(DocumentDirtyEvent{});

    state.app_state.status_message = "Undid: " + target.target_command_name + draft_note;
    return true;
}

} // namespace app
