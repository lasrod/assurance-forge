#pragma once

#include "app/areas/history_panel_area.h"
#include "app/areas/problems_area.h"
#include "app/areas/term_usages_area.h"
#include "imgui.h"

#include <functional>

namespace app {
struct AppRuntimeState;
namespace frame {
struct AppLayoutRegion;
}
} // namespace app

namespace app::areas {

struct FeedbackDockAreaCallbacks {
    ProblemsAreaCallbacks problems;
    TermUsagesAreaCallbacks term_usages;
    HistoryPanelAreaCallbacks history;
    std::function<void()> render_review_content;
    std::function<void()> render_ai_debug_content;
};

void RenderFeedbackDockArea(AppRuntimeState& state,
                            const frame::AppLayoutRegion& region,
                            ImGuiWindowFlags panel_flags,
                            const FeedbackDockAreaCallbacks& callbacks);

} // namespace app::areas