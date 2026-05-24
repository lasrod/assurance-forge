#pragma once

#include "app/app_runtime_state.h"
#include "core/commands/command_bus.h"

#include <string>

// Bridges the app-actions layer to `core::commands::CommandBus`. Every
// model mutation triggered by a menu action / button click should flow
// through `DispatchAuditedCommand` so that it is auto-saved and recorded
// in the audit timeline. This mirrors `ElementEditController`'s direct
// use of the bus but is callable from non-controller action classes.
namespace app::commands {

struct DispatchOutcome {
    bool        success = false;
    std::string error;
};

// Run `command` through the project's audit bus. Returns failure if no
// bus or no loaded model is available. Emits `AutosaveFailedEvent` to
// surface (or clear) the autosave banner; does NOT emit DocumentDirty /
// TreeDirty / StatusMessage events — callers decide whether to do so.
DispatchOutcome DispatchAuditedCommand(AppRuntimeState& state, core::commands::ICommand& command);

} // namespace app::commands
