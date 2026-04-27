#pragma once

#include "imgui.h"

#include "core/project_model.h"

#include <functional>

namespace ui::panels {

struct ProjectFilesPanelModel {
    const core::AssuranceProject* project = nullptr;
};

struct ProjectFilesPanelCallbacks {
    std::function<void()> add_sacm_file;
    std::function<void()> add_evidence_register;
    std::function<void()> add_j3377_cae_register;
    std::function<void(const core::ProjectFileEntry&)> open_file;
};

void ShowProjectFilesPanel(float width,
                           float height,
                           float top_y,
                           ImGuiWindowFlags panel_flags,
                           ProjectFilesPanelModel model,
                           const ProjectFilesPanelCallbacks& callbacks);

}  // namespace ui::panels
