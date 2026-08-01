#pragma once

#include <functional>

namespace app {
struct AppRuntimeState;
} // namespace app

namespace app::areas {

struct HistoryPanelAreaCallbacks {
    // Reserved for future use (e.g. element-history filter actions).
    std::function<void()> reserved;
};

// Render the contents of the FeedbackDock "History" tab: a read-only
// transactions table sourced from the current project's audit store.
// Selecting a row pins the active package canvas to that transaction
// sequence via `tab.timeline.preview_sequence`. "Return to live" clears
// the selection.
void RenderHistoryPanelContent(AppRuntimeState& state, const HistoryPanelAreaCallbacks& callbacks);

} // namespace app::areas
