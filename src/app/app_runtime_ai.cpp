#include "app/app_runtime.h"

#include "app/app_runtime_state.h"
#include "ui/ui_state.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <utility>

namespace app {

void AppRuntime::BeginAiReviewForSelection() {
    impl_->ai_review_controller->BeginReviewForSelection(
        GetLoadedCase(), impl_->current_tree, ui::GetUiState().selected_element_id);
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
