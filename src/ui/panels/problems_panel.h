#pragma once

#include "core/problems/problems_manager.h"
#include "imgui.h"
#include "ui/ui_state.h"

#include <functional>

namespace ui::panels {

struct ProblemsPanelModel {
    const core::ProblemsManager& problems_manager;
    ui::UiState& ui_state;
};

struct ProblemsPanelCallbacks {
    std::function<void(const core::ProblemItem&)> on_problem_activated;
    std::function<void(const core::ProblemItem&)> on_quick_fix;
    std::function<void(const core::ProblemItem&)> on_open_review;
};

void ShowProblemsPanel(float x,
                       float width,
                       float height,
                       float top_y,
                       ImGuiWindowFlags panel_flags,
                       ProblemsPanelModel model,
                       const ProblemsPanelCallbacks& callbacks);

void ShowProblemsPanelContent(ProblemsPanelModel model,
                              const ProblemsPanelCallbacks& callbacks,
                              bool show_title = true);

} // namespace ui::panels
