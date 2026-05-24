#include "app/commands/dispatch.h"

#include "app/app_events.h"
#include "core/audit/audit_event.h"
#include "parser/xml_parser.h"

namespace app::commands {

DispatchOutcome DispatchAuditedCommand(AppRuntimeState& state, core::commands::ICommand& command) {
    if (!state.app_state.sacm_package.has_value()) {
        return {false, "No SACM model loaded."};
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
