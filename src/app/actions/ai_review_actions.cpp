#include "app/actions/ai_review_actions.h"

#include "app/app_runtime_state.h"
#include "ui/ui_state.h"

namespace app {
namespace {

const parser::AssuranceCase* GetLoadedCase(const AppRuntimeState& state) {
    return state.app_state.loaded_case.has_value() ? &state.app_state.loaded_case.value() : nullptr;
}

} // namespace

AiReviewActions::AiReviewActions(AppRuntimeState& state) : state_(state) {}

void AiReviewActions::BeginForSelection() {
    state_.ai.review_controller->BeginReviewForSelection(
        GetLoadedCase(state_), state_.current_tree, ui::GetUiState().selected_element_id);
}

void AiReviewActions::BeginForSelection(const std::string& review_profile_id) {
    state_.ai.review_controller->BeginReviewForSelection(
        GetLoadedCase(state_), state_.current_tree, ui::GetUiState().selected_element_id, review_profile_id);
}

void AiReviewActions::RunForSelection(const std::string& review_profile_id) {
    state_.ai.review_controller->CancelPendingRequest();
    BeginForSelection(review_profile_id);
    state_.ai.review_controller->StartPendingRequest();
}

void AiReviewActions::StartPendingRequest() {
    state_.ai.review_controller->StartPendingRequest();
}

void AiReviewActions::PollTask() {
    state_.ai.review_controller->PollTask();
}

} // namespace app