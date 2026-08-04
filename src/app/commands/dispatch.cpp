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

DispatchOutcome
DispatchAuditedCommand(AppRuntimeState& state, core::commands::ICommand& command, const std::string& author) {
    if (!state.app_state.sacm_package.has_value()) {
        return {false, "No SACM model loaded."};
    }

    if (IsActiveCanvasInHistoricalPreview(state)) {
        return {false, "Cannot edit while viewing history. Return to Latest to make changes."};
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
        core::audit::AuditEvent unused_event;
        std::string apply_error;
        if (!command.Apply(ctx, unused_event, apply_error)) {
            return {false, apply_error};
        }
        // No bus, so `ctx` carried no library document and the command took the
        // legacy in-place path, mutating `sacm_package` but not the library-owned
        // document. Re-derive the library from the package so it stays the source
        // of truth even outside a project/audit context (the bus's Stage-5 net does
        // this on the audited path; the ACP controller used to do it itself before
        // its edits were routed through the bus).
        state.app_state.sync_library_document();
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
                                       state.app_state.sacm_package.value(),
                                       state.app_state.library_document.get()};
    // The library-primary flip is live again. The command bus no longer rebuilds
    // `loaded_case`/`sacm_package` mid-dispatch (that wholesale replace freed
    // containers the canvas was still rendering from -- the create-a-Claim crash).
    // It derives what it saves and hashes into a scratch copy instead; when the
    // flip engaged we re-derive the live views from the library at the next frame
    // boundary, the same deferred-to-next-frame remedy `pending_reconcile_audit_store`
    // uses for the identical mid-render container-teardown hazard.
    const auto result = state.command_bus->Execute(command, ctx, author);
    if (ctx.library_primary) {
        state.rederive_views_from_library = true;
        state.tree_needs_rebuild = true;
    }

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

    // Only `sacm_written` means the file matches the model. `result.success` is
    // still true when the autosave write itself failed, so gating on it here
    // would tell the user their work is saved when it is not.
    if (result.sacm_written) {
        state.app_state.has_unsaved_changes = false;
        state.autosave_persisted_pending_edit = true;
    }
    return {true, {}, result.sacm_written, result.transaction_sequence};
}

} // namespace app::commands
