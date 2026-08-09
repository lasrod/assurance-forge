#pragma once

#include "core/project_model.h"
#include "imgui.h"
#include "legacy_sacm/sacm_package_tree.h"

#include <functional>

namespace app {
struct AppRuntimeState;
namespace frame {
struct AppLayoutRegion;
}
} // namespace app

namespace app::areas {

struct ProjectExplorerAreaCallbacks {
    std::function<void()> refresh_sacm_package_tree_cache;
    std::function<void()> open_overview;
    std::function<void()> open_reviews;
    std::function<void()> open_cse_register;
    std::function<void()> open_evidence_register;
    std::function<void(const char*)> show_not_implemented;
    std::function<void()> create_sacm_file;
    std::function<void()> create_evidence_register;
    std::function<void()> create_j3377_cae_register;
    std::function<void(const core::ProjectFileEntry&)> open_file;
    std::function<void(const core::ProjectFileEntry&)> remove_file;
    std::function<void(const core::ProjectFileEntry&)> reveal_in_file_explorer;
    std::function<void(const core::ProjectFileEntry&, const sacm::SacmPackageTreeNode&)> open_package_node;
    std::function<void(const core::ProjectFileEntry&, const sacm::SacmPackageTreeNode&)> add_terminology_package;
    std::function<void(const core::ProjectFileEntry&, const sacm::SacmPackageTreeNode&)> remove_package;
};

void RenderProjectExplorerArea(AppRuntimeState& state,
                               const frame::AppLayoutRegion& region,
                               ImGuiWindowFlags panel_flags,
                               const ProjectExplorerAreaCallbacks& callbacks);

} // namespace app::areas
