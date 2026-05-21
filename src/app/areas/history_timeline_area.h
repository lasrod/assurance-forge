#pragma once

namespace app {
struct AppRuntimeState;
}

namespace app::areas {

struct WorkbenchAreaCallbacks;

// Renders the History Timeline tab content. Owns the small reconstruction
// cache used to populate the panel's "reconstructed state" summary so the
// replayer is not re-run every frame.
void RenderHistoryTimelineArea(AppRuntimeState& state, const WorkbenchAreaCallbacks& callbacks);

} // namespace app::areas
