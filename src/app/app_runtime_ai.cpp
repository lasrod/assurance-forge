#include "app/app_runtime.h"
#include "app/app_runtime_state.h"

#include "imgui.h"
#include "ui/ui_state.h"

#include <algorithm>

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

void AppRuntime::RenderAiReviewDebugModal() {
    if (!impl_->ai_review_controller->ShouldShowDebugModal()) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(920.0f, 700.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("AI Review Debug Request", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("Inspect the exact AI review request data before sending.");
        ImGui::Spacing();

        const float child_height = std::max(240.0f, ImGui::GetContentRegionAvail().y - 58.0f);
        ImGui::BeginChild("##ai_review_debug_text", ImVec2(0.0f, child_height), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(impl_->ai_review_controller->PendingDebugText().c_str());
        ImGui::EndChild();
        ImGui::Spacing();

        const float button_width = 110.0f;
        if (ImGui::Button("OK", ImVec2(button_width, 0.0f))) {
            impl_->ai_review_controller->SetDebugModalVisible(false);
            ImGui::CloseCurrentPopup();
            StartPendingAiReviewRequest();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(button_width, 0.0f))) {
            impl_->ai_review_controller->CancelPendingRequest();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else if (impl_->ai_review_controller->ShouldShowDebugModal()) {
        ImGui::OpenPopup("AI Review Debug Request");
    }
}

}  // namespace app
