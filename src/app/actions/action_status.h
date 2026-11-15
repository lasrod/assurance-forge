#pragma once

#include "app/app_events.h"
#include "app/app_runtime_state.h"

#include <string>

namespace app::actions {

// Reports `message` in the status bar. `message` is shown as given, so a caller
// translates it first; `status_message_i18n_check` fails on a raw literal here.
inline void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

} // namespace app::actions
