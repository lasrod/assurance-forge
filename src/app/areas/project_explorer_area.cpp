#include "app/areas/project_explorer_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "ui/panels/project_files_panel.h"

namespace app {

void RenderProjectExplorerArea(AppRuntimeState& state,
                               const AppLayoutRegion& region,
                               ImGuiWindowFlags panel_flags,
                               const ProjectExplorerAreaCallbacks& callbacks) {
    ui::panels::ProjectFilesPanelModel project_model;
    project_model.project =
        state.app_state.current_project.has_value() ? &state.app_state.current_project.value() : nullptr;
    if (project_model.project) {
        if (callbacks.refresh_sacm_package_tree_cache)
            callbacks.refresh_sacm_package_tree_cache();
        project_model.sacm_package_trees_by_path = state.sacm_package_tree_cache;
        const parser::AssuranceCase* loaded_case =
            state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr;
        for (const core::reviews::ReviewProposalSummary& summary :
             state.proposal_controller->manager.ListProposals(loaded_case)) {
            project_model.proposal_validity_by_path[summary.relative_path.generic_string()] = summary.validity;
        }
    }

    ui::panels::ProjectFilesPanelCallbacks project_callbacks{
        callbacks.create_sacm_file,
        callbacks.create_evidence_register,
        callbacks.create_j3377_cae_register,
        callbacks.open_file,
        callbacks.open_package_node,
        callbacks.add_terminology_package,
    };
    ui::panels::ShowProjectFilesPanel(
        region.size.x, region.size.y, region.pos.y, panel_flags, project_model, project_callbacks);
}

} // namespace app