#include "app/areas/feedback_dock_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "ui/panels/problems_panel.h"
#include "ui/panels/terminology_usages_panel.h"
#include "ui/ui_state.h"

namespace app::areas {

void RenderFeedbackDockArea(AppRuntimeState& state,
                            const frame::AppLayoutRegion& region,
                            ImGuiWindowFlags panel_flags,
                            const FeedbackDockAreaCallbacks& callbacks) {
    ui::panels::ProblemsPanelModel model{
        state.problems_manager,
        ui::GetUiState(),
    };
    ui::panels::ProblemsPanelCallbacks problem_callbacks{
        callbacks.activate_problem,
        callbacks.quick_fix_problem,
    };

    ImGui::SetNextWindowPos(region.pos);
    ImGui::SetNextWindowSize(region.size);
    ImGui::Begin("Problems and Review", nullptr, panel_flags | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::BeginTabBar("##problems_review_tabs")) {
        if (ImGui::BeginTabItem("Problems")) {
            ui::panels::ShowProblemsPanelContent(model, problem_callbacks, false);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags terminology_usage_flags =
            state.terminology.focus_usages_tab ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem("Term Usages", nullptr, terminology_usage_flags)) {
            ui::panels::TerminologyUsagesPanelModel usage_model;
            usage_model.has_search = state.terminology.usages_active;
            usage_model.term_value = state.terminology.usage_search_term_value;
            usage_model.term_name = state.terminology.usage_search_term_name;
            usage_model.message = state.terminology.usage_search_message;
            usage_model.error = state.terminology.usage_search_error;
            usage_model.usages = &state.terminology.usage_results;
            usage_model.selected_usage_index = state.terminology.selected_usage_index;

            ui::panels::TerminologyUsagesPanelCallbacks usage_callbacks;
            usage_callbacks.select_usage = [&state](std::size_t usage_index) {
                if (usage_index < state.terminology.usage_results.size())
                    state.terminology.selected_usage_index = static_cast<int>(usage_index);
            };
            usage_callbacks.activate_usage = callbacks.activate_terminology_usage;
            ui::panels::ShowTerminologyUsagesPanelContent(usage_model, usage_callbacks);
            ImGui::EndTabItem();
        }
        state.terminology.focus_usages_tab = false;

        if (ImGui::BeginTabItem("Review")) {
            if (callbacks.render_review_content)
                callbacks.render_review_content();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("AI Debug")) {
            if (callbacks.render_ai_debug_content)
                callbacks.render_ai_debug_content();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace app::areas