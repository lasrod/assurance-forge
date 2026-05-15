#include "app/areas/inspector_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "ui/panels/element_panel.h"

namespace app::areas {

void RenderInspectorArea(AppRuntimeState& state,
                         const frame::AppLayoutRegion& region,
                         ImGuiWindowFlags panel_flags,
                         const InspectorAreaCallbacks& callbacks) {
    ImGui::SetNextWindowPos(region.pos);
    ImGui::SetNextWindowSize(region.size);
    ImGui::Begin("Element Properties", nullptr, panel_flags);

    if (state.proposal_controller->creator_active) {
        if (callbacks.render_proposal_element_editor)
            callbacks.render_proposal_element_editor();
    } else if (state.proposal_controller->preview_active) {
        ImGui::TextWrapped("Proposal preview is active. Exit preview before editing element properties.");
    } else {
        parser::AssuranceCase* loaded_case =
            state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr;
        sacm::AssuranceCasePackage* sacm_package =
            state.app_state.sacm_package.has_value() ? &state.app_state.sacm_package.value() : nullptr;
        ui::panels::ElementTerminologyAssistCallbacks terminology_callbacks;
        terminology_callbacks.define_term = callbacks.define_terminology_term;
        terminology_callbacks.link_existing_term = callbacks.link_existing_terminology_term;
        terminology_callbacks.use_term_for_element = callbacks.use_terminology_term_for_element;
        terminology_callbacks.ignore_term = callbacks.ignore_terminology_suggestion;
        terminology_callbacks.is_ignored = callbacks.is_terminology_suggestion_ignored;
        terminology_callbacks.focus_review_tab = callbacks.focus_review_tab;
        if (ui::panels::ShowElementPanel(loaded_case, sacm_package, &terminology_callbacks)) {
            if (callbacks.mark_element_modified)
                callbacks.mark_element_modified();
        }
    }

    ImGui::End();
}

} // namespace app::areas
