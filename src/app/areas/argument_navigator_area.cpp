#include "app/areas/argument_navigator_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "imgui.h"
#include "ui/i18n/localization.h"

namespace app::areas {
namespace {

constexpr const char* kArgumentNavigatorTitle = "Argument Navigator";

ui::ElementContextActions MakeNavigatorContextActions(const AppRuntimeState& state,
                                                      const ArgumentNavigatorAreaCallbacks& callbacks) {
    if (state.proposal_controller->preview_active)
        return ui::ElementContextActions{};

    if (state.proposal_controller->creator_active) {
        return ui::ElementContextActions{
            callbacks.add_proposal_child,
            callbacks.add_proposal_top_goal,
            nullptr,
            nullptr,
            nullptr,
            callbacks.remove_proposal_selected,
            nullptr,
            callbacks.show_not_implemented,
        };
    }

    return callbacks.make_element_context_actions ? callbacks.make_element_context_actions()
                                                  : ui::ElementContextActions{};
}

} // namespace

void RenderArgumentNavigatorArea(AppRuntimeState& state,
                                 const frame::AppLayoutRegion& region,
                                 ImGuiWindowFlags panel_flags,
                                 const ArgumentNavigatorAreaCallbacks& callbacks) {
    ImGui::SetNextWindowPos(region.pos);
    ImGui::SetNextWindowSize(region.size);
    ImGui::Begin((AF_TR(kArgumentNavigatorTitle) + "###" + kArgumentNavigatorTitle).c_str(), nullptr, panel_flags);

    const bool proposal_canvas_active = state.IsProposalCanvasActive();
    ui::ElementContextActions actions = MakeNavigatorContextActions(state, callbacks);
    const parser::AssuranceCase* visible_case =
        proposal_canvas_active
            ? &state.proposal_controller->preview_model
            : (state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr);
    const ui::TreeEditActions* edit_actions = proposal_canvas_active ? nullptr : &callbacks.tree_edit_actions;
    ui::ShowTreeViewPanel(
        state.current_tree.root ? &state.current_tree : nullptr, visible_case, ui::GetUiState(), actions, edit_actions);
    ImGui::End();
}

} // namespace app::areas