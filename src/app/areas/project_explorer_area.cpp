#include "app/areas/project_explorer_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "core/perf/frame_profiler.h"
#include "ui/panels/project_explorer_panel.h"

namespace app::areas {
namespace {

ui::panels::ProjectExplorerPanelModel BuildProjectExplorerModel(AppRuntimeState& state,
                                                                const ProjectExplorerAreaCallbacks& callbacks) {
    ui::panels::ProjectExplorerPanelModel model;
    model.project = state.app_state.current_project.has_value() ? &state.app_state.current_project.value() : nullptr;
    if (!model.project)
        return model;

    if (callbacks.refresh_sacm_package_tree_cache) {
        core::perf::ScopedTimer s("app.pe.refresh_sacm_tree_cache");
        callbacks.refresh_sacm_package_tree_cache();
    }

    {
        core::perf::ScopedTimer s("app.pe.copy_tree_cache");
        model.sacm_package_trees_by_path = state.sacm_package_tree_cache;
    }
    const parser::AssuranceCase* loaded_case =
        state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr;
    {
        core::perf::ScopedTimer s("app.pe.list_proposals");
        for (const core::reviews::ReviewProposalSummary& summary :
             state.proposal_controller->manager.ListProposals(loaded_case)) {
            model.proposal_validity_by_path[summary.relative_path.generic_string()] = summary.validity;
        }
    }
    return model;
}

ui::panels::ProjectExplorerPanelCallbacks
MakeProjectExplorerPanelCallbacks(const ProjectExplorerAreaCallbacks& callbacks) {
    return ui::panels::ProjectExplorerPanelCallbacks{
        callbacks.create_sacm_file,
        callbacks.create_evidence_register,
        callbacks.create_j3377_cae_register,
        callbacks.open_file,
        callbacks.remove_file,
        callbacks.reveal_in_file_explorer,
        callbacks.open_package_node,
        callbacks.add_terminology_package,
        callbacks.add_pattern,
        callbacks.remove_package,
    };
}

} // namespace

void RenderProjectExplorerArea(AppRuntimeState& state,
                               const frame::AppLayoutRegion& region,
                               ImGuiWindowFlags panel_flags,
                               const ProjectExplorerAreaCallbacks& callbacks) {
    ui::panels::ProjectExplorerPanelModel model;
    {
        core::perf::ScopedTimer s("app.pe.build_model");
        model = BuildProjectExplorerModel(state, callbacks);
    }
    {
        core::perf::ScopedTimer s("app.pe.show_panel");
        ui::panels::ShowProjectExplorerPanel(region.size.x,
                                             region.size.y,
                                             region.pos.y,
                                             panel_flags,
                                             model,
                                             MakeProjectExplorerPanelCallbacks(callbacks));
    }
}

} // namespace app::areas
