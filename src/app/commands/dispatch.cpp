#include "app/commands/dispatch.h"

#include "app/app_events.h"
#include "core/audit/audit_event.h"
#include "parser/xml_parser.h"

namespace app::commands {

namespace {

// Phase 4.3 — single chokepoint read-only enforcement. When the active
// canvas tab is scrubbed to a historical sequence, any mutating command
// must be refused: the canvas the user is looking at is a reconstruction
// of a past model, while `state.app_state.loaded_case` still points at
// the LATEST model. Letting a command through would silently mutate the
// live model from a view of historical data — exactly the data-loss
// hazard the inspector read-only guard already prevents for text fields.
bool IsActiveCanvasInHistoricalPreview(const AppRuntimeState& state) {
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

DispatchOutcome DispatchAuditedCommand(AppRuntimeState& state, core::commands::ICommand& command) {
    if (!state.app_state.sacm_package.has_value()) {
        return {false, "No SACM model loaded."};
    }

    if (IsActiveCanvasInHistoricalPreview(state)) {
        return {false,
                "Cannot edit while viewing history. Return to Latest to make changes."};
    }

    // No bus available (e.g. unit tests, or a SACM file opened outside of a
    // project context). Fall back to a plain `Apply` so the mutation still
    // happens; nothing is appended to an audit log. Some commands only
    // mutate the SACM package and never touch the parser model, so we
    // tolerate a missing `loaded_case` by supplying a scratch instance.
    if (!state.command_bus) {
        parser::AssuranceCase scratch_model;
        parser::AssuranceCase& model_ref =
            state.app_state.loaded_case.has_value() ? state.app_state.loaded_case.value() : scratch_model;
        core::commands::CommandContext ctx{model_ref, state.app_state.sacm_package.value()};
        core::audit::AuditEvent        unused_event;
        std::string                    apply_error;
        if (!command.Apply(ctx, unused_event, apply_error)) {
            return {false, apply_error};
        }
        // The model has been mutated in-place but we have no bus to drive
        // the autosave path. Set the dirty flag directly (matching how
        // helpers like `EnsureQuickDefineTargetPackage` mark state) so
        // `SaveProject` and the close-prompt watchers will persist the
        // change. We deliberately do NOT emit `DocumentDirtyEvent` here:
        // many callers emit that event themselves after the dispatch
        // returns, and double-emission throws off subscribers that count
        // edits (and breaks unit tests that assert a single event per
        // logical user action).
        state.app_state.mark_dirty();
        state.document_dirty = true;
        return {true, {}};
    }

    if (!state.app_state.loaded_case.has_value()) {
        return {false, "No SACM model loaded."};
    }

    core::commands::CommandContext ctx{state.app_state.loaded_case.value(),
                                       state.app_state.sacm_package.value()};
    const auto result = state.command_bus->Execute(command, ctx, {});

    // Mirror ElementEditController::EmitAutosaveStatus semantics: a non-empty
    // error is always user-visible; a clean success clears any stale banner.
    if (!result.error.empty()) {
        state.events.Emit(AutosaveFailedEvent{result.error});
    } else if (result.success) {
        state.events.Emit(AutosaveFailedEvent{std::string{}});
    }

    if (!result.success) {
        return {false, result.error};
    }
    return {true, {}};
}

} // namespace app::commands
