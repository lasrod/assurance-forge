#include "app/areas/argument_navigator_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "imgui.h"

namespace app {

void RenderArgumentNavigatorArea(AppRuntimeState& state,
                                 const AppLayoutRegion& region,
                                 ImGuiWindowFlags panel_flags,
                                 const ArgumentNavigatorAreaCallbacks& callbacks) {
    ImGui::SetNextWindowPos(region.pos);
    ImGui::SetNextWindowSize(region.size);
    ImGui::Begin("Safety Case Tree", nullptr, panel_flags);

    ui::ElementContextActions actions;
    if (state.proposal_controller->preview_active) {
        actions = ui::ElementContextActions{};
    } else if (state.proposal_controller->creator_active) {
        actions = ui::ElementContextActions{
            callbacks.add_proposal_child,
            callbacks.add_proposal_top_goal,
            callbacks.remove_proposal_selected,
            nullptr,
            callbacks.show_not_implemented,
        };
    } else if (callbacks.make_element_context_actions) {
        actions = callbacks.make_element_context_actions();
    }

    const parser::AssuranceCase* visible_case =
        state.IsProposalCanvasActive()
            ? &state.proposal_controller->preview_model
            : (state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr);
    const ui::TreeEditActions* edit_actions = state.IsProposalCanvasActive() ? nullptr : &callbacks.tree_edit_actions;
    ui::ShowTreeViewPanel(
        state.current_tree.root ? &state.current_tree : nullptr, visible_case, ui::GetUiState(), actions, edit_actions);
    ImGui::End();
}

} // namespace app