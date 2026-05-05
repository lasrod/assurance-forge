#pragma once

#include "core/project_model.h"
#include "core/reviews/review_proposal.h"
#include "imgui.h"

#include <functional>
#include <map>
#include <string>

namespace ui::panels {

struct ProjectFilesPanelModel {
    const core::AssuranceProject* project = nullptr;
    std::map<std::string, core::reviews::ProposalValidityResult> proposal_validity_by_path;
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

} // namespace ui::panels
