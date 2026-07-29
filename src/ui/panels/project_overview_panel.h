#pragma once

#include "core/project_model.h"
#include "core/project_summary.h"

#include <functional>
#include <string>

namespace ui::panels {

struct ProjectOverviewPanelModel {
    const core::AssuranceProject* project = nullptr;
    core::ProjectSummary summary;
    std::string active_case_name;
};

struct ProjectOverviewPanelCallbacks {
    std::function<void()> open_arguments;
    std::function<void()> open_evidence;
    std::function<void()> open_reviews;
    std::function<void()> open_conformance;
};

void ShowProjectOverviewPanel(const ProjectOverviewPanelModel& model,
                              const ProjectOverviewPanelCallbacks& callbacks);

} // namespace ui::panels
