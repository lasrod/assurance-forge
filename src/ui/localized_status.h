// The status message of a `core::AppState`, in the user's language.
#pragma once

#include <string>

namespace core {
struct AppState;
}

namespace ui {

// `core` reports its load/save messages as an English msgid plus arguments
// (`core::AppState::status_source`); this translates the msgid and fills the
// arguments in. Text written straight into `status_message` -- by `app`, which
// translates where it sets -- is returned unchanged.
//
// Translated on every call, so a `core` message already on screen follows a
// language switch.
std::string LocalizedStatusMessage(const core::AppState& app_state);

} // namespace ui
