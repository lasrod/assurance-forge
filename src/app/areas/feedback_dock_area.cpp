#include "app/areas/feedback_dock_area.h"

#include "app/app_runtime_state.h"
#include "app/frame/app_layout_regions.h"
#include "ui/i18n/localization.h"
#include "ui/ui_state.h"

namespace app::areas {

void RenderFeedbackDockArea(AppRuntimeState& state,
                            const frame::AppLayoutRegion& region,
                            ImGuiWindowFlags panel_flags,
                            const FeedbackDockAreaCallbacks& callbacks) {
    ImGui::SetNextWindowPos(region.pos);
    ImGui::SetNextWindowSize(region.size);
    ImGui::Begin("Problems and Review", nullptr, panel_flags | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::BeginTabBar("##problems_review_tabs")) {
        ui::UiState& ui_state = ui::GetUiState();
        ImGuiTabItemFlags problems_flags = ui_state.problems_panel_open_pending ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(AF_TR("Problems").c_str(), nullptr, problems_flags)) {
            RenderProblemsAreaContent(state, callbacks.problems);
            ImGui::EndTabItem();
        }
        ui_state.problems_panel_open_pending = false;

        ImGuiTabItemFlags terminology_usage_flags =
            state.terminology.focus_usages_tab ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(AF_TR("Term Usages").c_str(), nullptr, terminology_usage_flags)) {
            RenderTermUsagesAreaContent(state, callbacks.term_usages);
            ImGui::EndTabItem();
        }
        state.terminology.focus_usages_tab = false;

        ImGuiTabItemFlags review_flags = state.workbench.focus_review_tab ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(AF_TR("Review").c_str(), nullptr, review_flags)) {
            if (callbacks.render_review_content)
                callbacks.render_review_content();
            ImGui::EndTabItem();
        }
        state.workbench.focus_review_tab = false;

        if (ImGui::BeginTabItem(AF_TR("History").c_str(),
                                nullptr,
                                state.workbench.focus_history_tab ? ImGuiTabItemFlags_SetSelected : 0)) {
            RenderHistoryPanelContent(state, callbacks.history);
            ImGui::EndTabItem();
        }
        state.workbench.focus_history_tab = false;

        if (ImGui::BeginTabItem(AF_TR("AI Debug").c_str())) {
            if (callbacks.render_ai_debug_content)
                callbacks.render_ai_debug_content();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace app::areas