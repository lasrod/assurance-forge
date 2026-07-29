#pragma once

#include "core/project_model.h"
#include "core/project_summary.h"
#include "imgui.h"
#include "sacm/sacm_package_tree.h"

#include <functional>
#include <map>
#include <string>

namespace ui::panels {

struct ProjectExplorerPanelModel {
    const core::AssuranceProject* project = nullptr;
    core::ProjectSummary summary;
    bool overview_selected = false;
    bool cse_register_selected = false;
    bool evidence_register_selected = false;
    std::filesystem::path active_file_path;
    std::map<std::string, sacm::SacmPackageTreeResult> sacm_package_trees_by_path;
};

struct ProjectExplorerPanelCallbacks {
    std::function<void()> open_overview;
    std::function<void()> open_reviews;
    std::function<void()> open_cse_register;
    std::function<void()> open_evidence_register;
    std::function<void(const char*)> show_not_implemented;
    std::function<void()> add_sacm_file;
    std::function<void()> add_evidence_register;
    std::function<void()> add_j3377_cae_register;
    std::function<void(const core::ProjectFileEntry&)> open_file;
    std::function<void(const core::ProjectFileEntry&)> remove_file;
    std::function<void(const core::ProjectFileEntry&)> reveal_in_file_explorer;
    std::function<void(const core::ProjectFileEntry&, const sacm::SacmPackageTreeNode&)> open_package_node;
    std::function<void(const core::ProjectFileEntry&, const sacm::SacmPackageTreeNode&)> add_terminology_package;
    std::function<void(const core::ProjectFileEntry&, const sacm::SacmPackageTreeNode&)> remove_package;
};

void ShowProjectExplorerPanel(float width,
                              float height,
                              float top_y,
                              ImGuiWindowFlags panel_flags,
                              ProjectExplorerPanelModel model,
                              const ProjectExplorerPanelCallbacks& callbacks);

} // namespace ui::panels
