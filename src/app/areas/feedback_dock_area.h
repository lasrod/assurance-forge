#pragma once

#include "core/problems/problem_item.h"
#include "imgui.h"

#include <cstddef>
#include <functional>

namespace app {
struct AppRuntimeState;
namespace frame { struct AppLayoutRegion; }
}

namespace app::areas {

struct FeedbackDockAreaCallbacks {
    std::function<void(const core::ProblemItem&)> activate_problem;
    std::function<void(const core::ProblemItem&)> quick_fix_problem;
    std::function<void(std::size_t)> activate_terminology_usage;
    std::function<void()> render_review_content;
    std::function<void()> render_ai_debug_content;
};

void RenderFeedbackDockArea(AppRuntimeState& state,
                            const frame::AppLayoutRegion& region,
                            ImGuiWindowFlags panel_flags,
                            const FeedbackDockAreaCallbacks& callbacks);

} // namespace app::areas