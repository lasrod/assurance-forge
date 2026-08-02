#include "app/actions/ai_review_actions.h"

#include "app/app_runtime_state.h"
#include "ui/ui_state.h"

#include <optional>

namespace app::actions {
namespace {

struct AiReviewInput {
    const parser::AssuranceCase* model = nullptr;
    core::AssuranceTree tree;
    bool includes_working_draft = false;
};

std::optional<AiReviewInput> BuildAiReviewInput(AppRuntimeState& state) {
    AiReviewInput input;
    if (!state.app_state.loaded_case.has_value())
        return std::nullopt;

    input.model = &state.app_state.loaded_case.value();
    const core::drafts::DraftWorkspace* workspace = state.draft_workspace.workspace();
    if (workspace != nullptr && workspace->has_active_groups()) {
        const core::drafts::DraftMaterializationResult& result =
            state.draft_workspace.Materialize(state.app_state.loaded_case.value(), state.app_state.case_revision);
        if (!result.success) {
            state.events.Emit(StatusMessageEvent{
                "AI review cannot run because the working draft could not be materialized: " + result.error});
            return std::nullopt;
        }
        input.model = &result.working_model;
        input.includes_working_draft = true;
    }
    input.tree = core::AssuranceTree::Build(*input.model, ui::GetUiState().active_secondary_lang);
    return input;
}

} // namespace

AiReviewActions::AiReviewActions(AppRuntimeState& state) : state_(state) {}

void AiReviewActions::BeginForSelection() {
    const std::optional<AiReviewInput> input = BuildAiReviewInput(state_);
    if (!input.has_value())
        return;
    state_.ai.review_controller->BeginReviewForSelection(
        input->model, input->tree, ui::GetUiState().selected_element_id);
    if (input->includes_working_draft)
        state_.ai.review_controller->MarkPendingRequestIncludesWorkingDraft();
}

void AiReviewActions::BeginForSelection(const std::string& review_profile_id) {
    const std::optional<AiReviewInput> input = BuildAiReviewInput(state_);
    if (!input.has_value())
        return;
    state_.ai.review_controller->BeginReviewForSelection(
        input->model, input->tree, ui::GetUiState().selected_element_id, review_profile_id);
    if (input->includes_working_draft)
        state_.ai.review_controller->MarkPendingRequestIncludesWorkingDraft();
}

void AiReviewActions::RunForSelection() {
    state_.ai.review_controller->CancelPendingRequest();
    BeginForSelection();
    state_.ai.review_controller->StartPendingRequest();
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

} // namespace app::actions
