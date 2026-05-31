#include "app/areas/ai_debug_area.h"

#include "ai/ai_claim_review.h"
#include "app/actions/ai_review_actions.h"
#include "app/app_runtime_state.h"
#include "app/guideline_catalog.h"
#include "ui/i18n/localization.h"
#include "ui/ui_state.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <utility>

namespace app::areas {
namespace {

void EnsureGuidelineCatalogLoaded(::app::AppRuntimeState& state) {
    if (state.guideline_catalog_load_attempted)
        return;

    ::app::GuidelineCatalog catalog;
    std::string error;
    if (::app::LoadGuidelineCatalog(catalog, error)) {
        state.guideline_catalog = std::move(catalog);
        state.guideline_catalog_error.clear();
    } else {
        state.guideline_catalog.reset();
        state.guideline_catalog_error = error;
    }
    state.guideline_catalog_load_attempted = true;
}

void DrawTooltipIfHovered(const std::string& text) {
    if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text.c_str());
}

} // namespace

void RenderAiDebugPanelContent(::app::AppRuntimeState& state) {
    auto& ai_review = *state.ai.review_controller;
    const bool review_running = ai_review.IsReviewRunning();
    EnsureGuidelineCatalogLoaded(state);

    const ui::UiState& ui_state = ui::GetUiState();
    const parser::AssuranceCase* loaded_case =
        state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr;
    const parser::SacmElement* selected_element = loaded_case && !ui_state.selected_element_id.empty()
                                                      ? ai::FindSacmElement(*loaded_case, ui_state.selected_element_id)
                                                      : nullptr;
    const core::TreeNode* selected_node = core::FindTreeNode(state.current_tree, ui_state.selected_element_id);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float left_width = 130.0f;
    const float response_width = std::max(260.0f, available.x * 0.34f);
    const float prompt_width = std::max(260.0f, available.x - left_width - response_width - spacing * 2.0f);
    const float panel_height = std::max(120.0f, available.y);

    ImGui::BeginChild("##ai_debug_actions", ImVec2(left_width, panel_height), true);
    if (review_running)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("AI Review").c_str(), ImVec2(-1.0f, 0.0f))) {
        actions::AiReviewActions(state).BeginForSelection();
    }
    if (review_running)
        ImGui::EndDisabled();

    if (state.guideline_catalog.has_value()) {
        ImGui::Separator();
        for (const parser::ReviewProfile& profile : state.guideline_catalog->document.review_profiles) {
            const bool compatible =
                selected_element && ai::IsReviewProfileCompatibleWithElement(profile, *selected_element, selected_node);
            const bool enabled = !review_running && loaded_case && selected_element && compatible;
            std::string tooltip = profile.description;
            if (!loaded_case)
                tooltip = AF_TR("Open an assurance case before running SCCG profile reviews.");
            else if (!selected_element)
                tooltip = AF_TR("Select a GSN/SACM element before running SCCG profile reviews.");
            else if (!compatible)
                tooltip = profile.description + "\n\n" + AF_TR("This profile does not apply to the selected element type.");

            ImGui::PushID(profile.id.c_str());
            if (!enabled)
                ImGui::BeginDisabled();
            if (ImGui::Button(profile.display_name.c_str(), ImVec2(-1.0f, 0.0f))) {
                actions::AiReviewActions(state).BeginForSelection(profile.id);
            }
            DrawTooltipIfHovered(tooltip);
            if (!enabled)
                ImGui::EndDisabled();
            ImGui::PopID();
        }
    } else if (!state.guideline_catalog_error.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s",
                           ui::i18n::trf("SCCG profiles unavailable: {0}", state.guideline_catalog_error).c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##ai_debug_prompt_column", ImVec2(prompt_width, panel_height), false);
    ImGui::TextUnformatted(AF_TR("Prompt").c_str());
    std::string prompt = ai_review.PendingPrompt();
    const float send_row_height = ImGui::GetFrameHeightWithSpacing();
    const float prompt_height = std::max(80.0f, ImGui::GetContentRegionAvail().y - send_row_height);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextMultiline(
            "##ai_debug_prompt", &prompt, ImVec2(-1.0f, prompt_height), ImGuiInputTextFlags_AllowTabInput)) {
        ai_review.SetPendingPrompt(std::move(prompt));
    }

    const bool has_prompt = !ai_review.PendingPrompt().empty();
    if (!has_prompt || review_running)
        ImGui::BeginDisabled();
    if (ImGui::Button((review_running ? AF_TR("Sending...") : AF_TR("Send")).c_str())) {
        actions::AiReviewActions(state).StartPendingRequest();
    }
    if (!has_prompt || review_running)
        ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##ai_debug_response_column", ImVec2(0.0f, panel_height), false);
    ImGui::TextUnformatted(AF_TR("Response").c_str());
    if (review_running) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", AF_TR("waiting...").c_str());
    }
    if (!ai_review.LastParseError().empty()) {
        ImGui::TextWrapped("%s", ui::i18n::trf("Parse error: {0}", ai_review.LastParseError()).c_str());
    }
    std::string response = ai_review.LastRawResponse();
    if (response.empty())
        response = AF_TR("No response yet.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("##ai_debug_response", &response, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndChild();
}

} // namespace app::areas
