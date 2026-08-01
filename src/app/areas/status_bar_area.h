#pragma once

namespace app {
struct AppRuntimeState;
} // namespace app

namespace app::areas {

// Builds the status bar model from runtime state and renders it. The bar is
// pinned to the bottom of the display by the panel itself; the shell only needs
// to have reserved its height.
void RenderStatusBarArea(AppRuntimeState& state);

} // namespace app::areas
