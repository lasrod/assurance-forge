#include "app/app_runtime.h"

#include "app/app_runtime_state.h"
#include "app/guideline_catalog.h"
#include "ui/ui_state.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <utility>

namespace app {
namespace {

void EnsureAiGuidelineCatalogLoaded(AppRuntimeState& state) {
    if (state.guideline_catalog_load_attempted)
        return;

    GuidelineCatalog catalog;
    std::string error;
    if (LoadGuidelineCatalog(catalog, error)) {
        state.guideline_catalog = std::move(catalog);
        state.guideline_catalog_error.clear();
    } else {
        state.guideline_catalog.reset();
        state.guideline_catalog_error = error;
    }
    state.guideline_catalog_load_attempted = true;
}

const core::TreeNode* FindTreeNode(const core::AssuranceTree& tree, const std::string& element_id) {
    for (const auto& node : tree.nodes) {
        if (node && node->id == element_id)
            return node.get();
    }
    return nullptr;
}

void DrawTooltipIfHovered(const std::string& text) {
    if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text.c_str());
}

} // namespace

void AppRuntime::BeginAiReviewForSelection() {
    impl_->ai_review_controller->BeginReviewForSelection(
        GetLoadedCase(), impl_->current_tree, ui::GetUiState().selected_element_id);
}

void AppRuntime::RunAiReviewForSelection(const std::string& review_profile_id) {
    impl_->ai_review_controller->CancelPendingRequest();
    impl_->ai_review_controller->BeginReviewForSelection(
        GetLoadedCase(), impl_->current_tree, ui::GetUiState().selected_element_id, review_profile_id);
    impl_->ai_review_controller->StartPendingRequest();
}

void AppRuntime::RenderAiReviewContextMenuForSelected() {
    EnsureAiGuidelineCatalogLoaded(*impl_);

    const ui::UiState& ui_state = ui::GetUiState();
    const parser::AssuranceCase* loaded_case = GetLoadedCase();
    const parser::SacmElement* selected_element =
        loaded_case && !ui_state.selected_element_id.empty()
            ? ai::FindSacmElement(*loaded_case, ui_state.selected_element_id)
            : nullptr;
    const core::TreeNode* selected_node = FindTreeNode(impl_->current_tree, ui_state.selected_element_id);
    const bool review_running = impl_->ai_review_controller->IsReviewRunning();

    if (!ImGui::BeginMenu("AI Review"))
        return;

    if (!impl_->guideline_catalog.has_value()) {
        ImGui::TextDisabled("SCCG profiles unavailable.");
        if (!impl_->guideline_catalog_error.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", impl_->guideline_catalog_error.c_str());
        ImGui::EndMenu();
        return;
    }

    for (const parser::ReviewProfile& profile : impl_->guideline_catalog->document.review_profiles) {
        const bool compatible =
            selected_element && ai::IsReviewProfileCompatibleWithElement(profile, *selected_element, selected_node);
        const bool enabled = !review_running && loaded_case && selected_element && compatible;
        std::string tooltip = profile.description;
        if (review_running)
            tooltip = "AI review is already running.";
        else if (!loaded_case)
            tooltip = "Open an assurance case before running SCCG profile reviews.";
        else if (!selected_element)
            tooltip = "Select a GSN/SACM element before running SCCG profile reviews.";
        else if (!compatible)
            tooltip = profile.description + "\n\nThis profile does not apply to the selected element type.";

        ImGui::PushID(profile.id.c_str());
        if (ImGui::MenuItem(profile.display_name.c_str(), nullptr, false, enabled)) {
            RunAiReviewForSelection(profile.id);
        }
        DrawTooltipIfHovered(tooltip);
        ImGui::PopID();
    }

    ImGui::EndMenu();
}

void AppRuntime::StartPendingAiReviewRequest() {
    impl_->ai_review_controller->StartPendingRequest();
}

void AppRuntime::PollAiReviewTask() {
    impl_->ai_review_controller->PollTask();
}

void AppRuntime::RenderAiDebugPanelContent() {
    auto& ai_review = *impl_->ai_review_controller;
    const bool review_running = ai_review.IsReviewRunning();
    EnsureAiGuidelineCatalogLoaded(*impl_);

    const ui::UiState& ui_state = ui::GetUiState();
    const parser::AssuranceCase* loaded_case = GetLoadedCase();
    const parser::SacmElement* selected_element =
        loaded_case && !ui_state.selected_element_id.empty()
            ? ai::FindSacmElement(*loaded_case, ui_state.selected_element_id)
            : nullptr;
    const core::TreeNode* selected_node = FindTreeNode(impl_->current_tree, ui_state.selected_element_id);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float left_width = 130.0f;
    const float response_width = std::max(260.0f, available.x * 0.34f);
    const float prompt_width = std::max(260.0f, available.x - left_width - response_width - spacing * 2.0f);
    const float panel_height = std::max(120.0f, available.y);

    ImGui::BeginChild("##ai_debug_actions", ImVec2(left_width, panel_height), true);
    if (review_running)
        ImGui::BeginDisabled();
    if (ImGui::Button("AI Review", ImVec2(-1.0f, 0.0f))) {
        BeginAiReviewForSelection();
    }
    if (review_running)
        ImGui::EndDisabled();

    if (impl_->guideline_catalog.has_value()) {
        ImGui::Separator();
        for (const parser::ReviewProfile& profile : impl_->guideline_catalog->document.review_profiles) {
            const bool compatible =
                selected_element && ai::IsReviewProfileCompatibleWithElement(profile, *selected_element, selected_node);
            const bool enabled = !review_running && loaded_case && selected_element && compatible;
            std::string tooltip = profile.description;
            if (!loaded_case)
                tooltip = "Open an assurance case before running SCCG profile reviews.";
            else if (!selected_element)
                tooltip = "Select a GSN/SACM element before running SCCG profile reviews.";
            else if (!compatible)
                tooltip = profile.description + "\n\nThis profile does not apply to the selected element type.";

            ImGui::PushID(profile.id.c_str());
            if (!enabled)
                ImGui::BeginDisabled();
            if (ImGui::Button(profile.display_name.c_str(), ImVec2(-1.0f, 0.0f))) {
                impl_->ai_review_controller->BeginReviewForSelection(
                    loaded_case, impl_->current_tree, ui_state.selected_element_id, profile.id);
            }
            DrawTooltipIfHovered(tooltip);
            if (!enabled)
                ImGui::EndDisabled();
            ImGui::PopID();
        }
    } else if (!impl_->guideline_catalog_error.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("SCCG profiles unavailable: %s", impl_->guideline_catalog_error.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##ai_debug_prompt_column", ImVec2(prompt_width, panel_height), false);
    ImGui::TextUnformatted("Prompt");
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
    if (ImGui::Button(review_running ? "Sending..." : "Send")) {
        StartPendingAiReviewRequest();
    }
    if (!has_prompt || review_running)
        ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##ai_debug_response_column", ImVec2(0.0f, panel_height), false);
    ImGui::TextUnformatted("Response");
    if (review_running) {
        ImGui::SameLine();
        ImGui::TextDisabled("waiting...");
    }
    if (!ai_review.LastParseError().empty()) {
        ImGui::TextWrapped("Parse error: %s", ai_review.LastParseError().c_str());
    }
    std::string response = ai_review.LastRawResponse();
    if (response.empty())
        response = "No response yet.";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline(
        "##ai_debug_response", &response, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndChild();
}

} // namespace app
