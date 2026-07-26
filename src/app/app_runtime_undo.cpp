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

#include <string>

namespace app {

namespace {

// Look up the active argument-package canvas tab and decide if it is
// scrubbed into the history preview. Undo is blocked while previewing
// because the canvas the user is looking at does not reflect the live
// model. Mirrors the dispatch-layer guard in `app::commands::dispatch`.
bool ActiveCanvasInHistoricalPreview(const AppRuntimeState& state) {
    const std::string& active = state.workbench.active_argument_package_canvas_key;
    if (active.empty()) return false;
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
    if (!state.command_bus) return false;
    if (!state.app_state.current_project.has_value()) return false;
    if (!state.app_state.loaded_case.has_value()) return false;
    if (!state.app_state.has_projected_package()) return false;
    if (ActiveCanvasInHistoricalPreview(state)) return false;

    const auto& transactions = state.command_bus->Store().Transactions();
    const auto target        = core::audit::FindUndoTarget(transactions);
    if (!target.has_target) return false;

    const auto& project   = state.app_state.current_project.value();
    const auto& snapshots = areas::GetCachedSnapshots(project.rootPath);
    const auto& baselines = areas::GetCachedBaselines(project.rootPath);
    const auto boundary   = core::audit::FindUndoBoundary(snapshots, baselines, target.target_sequence);
    return core::audit::CanUndo(target.target_sequence, boundary);
}

bool AppRuntime::Undo() {
    AppRuntimeState& state = *impl_;

    if (!state.command_bus) {
        state.app_state.status_message = "Undo unavailable: no project audit bus.";
        return false;
    }
    if (!state.app_state.current_project.has_value() ||
        !state.app_state.loaded_case.has_value() ||
        !state.app_state.has_projected_package()) {
        state.app_state.status_message = "Undo unavailable: no project loaded.";
        return false;
    }
    if (ActiveCanvasInHistoricalPreview(state)) {
        state.app_state.status_message =
            "Cannot undo while viewing history. Return to Latest to make changes.";
        return false;
    }

    const auto& transactions = state.command_bus->Store().Transactions();
    const auto target        = core::audit::FindUndoTarget(transactions);
    if (!target.has_target) {
        state.app_state.status_message = "Nothing to undo.";
        return false;
    }

    const auto& project   = state.app_state.current_project.value();
    const auto& snapshots = areas::GetCachedSnapshots(project.rootPath);
    const auto& baselines = areas::GetCachedBaselines(project.rootPath);
    const auto boundary   = core::audit::FindUndoBoundary(snapshots, baselines, target.target_sequence);
    if (!core::audit::CanUndo(target.target_sequence, boundary)) {
        state.app_state.status_message =
            "Reached snapshot or baseline — restore from history to go further back.";
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
    core::commands::UndoLastTransactionCommand cmd(
        target.target_sequence, target.target_command_name,
        std::move(prior.value().views.model), std::move(prior.value().views.package),
        std::move(prior.value().document));

    const auto outcome = commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        state.app_state.status_message = "Undo failed: " + outcome.error;
        return false;
    }

    // The model has been wholesale-replaced. Rebuild derived views (tree,
    // canvas, registers) on the next frame and drop any selection that may
    // now reference a removed element. Mirrors the post-dispatch fan-out
    // performed by ElementEditController for ordinary mutations.
    state.events.Emit(TreeDirtyEvent{});
    state.events.Emit(SelectionChangedEvent{});
    state.events.Emit(DocumentDirtyEvent{});

    state.app_state.status_message = "Undid: " + target.target_command_name;
    return true;
}

} // namespace app
