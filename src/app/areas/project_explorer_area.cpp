#include "app/areas/project_explorer_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "core/perf/frame_profiler.h"
#include "core/project_summary.h"
#include "ui/panels/project_explorer_panel.h"
#include "ui/ui_state.h"

#include <vector>

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
    std::vector<core::reviews::ProposalValidityResult> proposal_validities;
    {
        core::perf::ScopedTimer s("app.pe.list_proposals");
        for (const core::reviews::ReviewProposalSummary& summary :
             state.proposal_controller->manager.ListProposals(loaded_case)) {
            proposal_validities.push_back(summary.validity);
        }
    }
    model.summary = core::BuildProjectSummary(model.project,
                                              loaded_case,
                                              loaded_case ? &state.current_tree : nullptr,
                                              state.problems_manager.GetProblems(),
                                              state.review_controller->Items(),
                                              proposal_validities);
    model.overview_selected = ui::GetUiState().center_view == ui::CenterView::ProjectOverview;
    model.cse_register_selected = ui::GetUiState().center_view == ui::CenterView::CseRegister;
    model.evidence_register_selected = ui::GetUiState().center_view == ui::CenterView::EvidenceRegister;
    model.active_file_path = state.app_state.active_project_file_path;
    return model;
}

ui::panels::ProjectExplorerPanelCallbacks
MakeProjectExplorerPanelCallbacks(const ProjectExplorerAreaCallbacks& callbacks) {
    return ui::panels::ProjectExplorerPanelCallbacks{
        callbacks.open_overview,
        callbacks.open_reviews,
        callbacks.open_cse_register,
        callbacks.open_evidence_register,
        callbacks.show_not_implemented,
        callbacks.create_sacm_file,
        callbacks.create_evidence_register,
        callbacks.create_j3377_cae_register,
        callbacks.open_file,
        callbacks.remove_file,
        callbacks.reveal_in_file_explorer,
        callbacks.open_package_node,
        callbacks.add_terminology_package,
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
