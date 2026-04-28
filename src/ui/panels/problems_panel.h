#pragma once

#include "imgui.h"

#include "core/problems/problems_manager.h"
#include "ui/ui_state.h"

#include <functional>

namespace ui::panels {

struct ProblemsPanelModel {
    const core::ProblemsManager& problems_manager;
    ui::UiState& ui_state;
};

struct ProblemsPanelCallbacks {
    std::function<void(const core::ProblemItem&)> on_problem_activated;
};

void ShowProblemsPanel(float x,
                       float width,
                       float height,
                       float top_y,
                       ImGuiWindowFlags panel_flags,
                       ProblemsPanelModel model,
                       const ProblemsPanelCallbacks& callbacks);

}  // namespace ui::panels
