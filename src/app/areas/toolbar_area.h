#pragma once

#include "ui/panels/toolbar_panel.h"

namespace app {
struct AppRuntimeState;
} // namespace app

namespace app::areas {

// Builds the toolbar model from runtime state and renders it below the menu
// bar. `callbacks` are the same handlers the menu items use, passed in by
// AppRuntime so a button and its menu item cannot diverge.
void RenderToolbarArea(AppRuntimeState& state, const ui::panels::ToolbarCallbacks& callbacks, float top_y);

} // namespace app::areas
