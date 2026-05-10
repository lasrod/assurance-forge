#pragma once

#include "core/project_model.h"
#include "core/reviews/review_proposal.h"
#include "imgui.h"
#include "sacm/sacm_package_tree.h"

#include <functional>
#include <map>
#include <string>

namespace ui::panels {

struct ProjectExplorerPanelModel {
    const core::AssuranceProject* project = nullptr;
    std::map<std::string, core::reviews::ProposalValidityResult> proposal_validity_by_path;
    std::map<std::string, sacm::SacmPackageTreeResult> sacm_package_trees_by_path;
};

struct ProjectExplorerPanelCallbacks {
    std::function<void()> add_sacm_file;
    std::function<void()> add_evidence_register;
    std::function<void()> add_j3377_cae_register;
    std::function<void(const core::ProjectFileEntry&)> open_file;
    std::function<void(const core::ProjectFileEntry&, const sacm::SacmPackageTreeNode&)> open_package_node;
    std::function<void(const core::ProjectFileEntry&, const sacm::SacmPackageTreeNode&)> add_terminology_package;
};

void ShowProjectExplorerPanel(float width,
                              float height,
                              float top_y,
                              ImGuiWindowFlags panel_flags,
                              ProjectExplorerPanelModel model,
                              const ProjectExplorerPanelCallbacks& callbacks);

} // namespace ui::panels
