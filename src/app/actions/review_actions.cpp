#include "app/actions/review_actions.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/project_workflow.h"
#include "core/project_service.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/ui_state.h"

#include <filesystem>
#include <string>

namespace app::actions {
namespace {

void ClearProposalHighlightState(ui::UiState& ui_state) {
    ui_state.proposal_highlight_ids.clear();
    ui_state.marked_for_removal.clear();
    ui_state.center_on_marked = false;
    ui_state.dim_non_proposal_nodes = false;
}

void RestoreBaseCanvas(AppRuntimeState& state) {
    ClearProposalHighlightState(ui::GetUiState());
    if (state.app_state.loaded_case.has_value()) {
        state.current_tree = ui::gsn::BuildAssuranceTree(state.app_state.loaded_case.value());
        ui::gsn::SetCanvasTree(state.current_tree);
    } else {
        state.tree_needs_rebuild = true;
    }
}

void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

} // namespace

ReviewActions::ReviewActions(AppRuntimeState& state) : state_(state) {}

bool ReviewActions::DeleteProposalPatchFile(const std::string& proposal_id, std::string& error) {
    if (!state_.app_state.current_project.has_value()) {
        error = "Open a project before deleting proposed changes.";
        return false;
    }

    core::AssuranceProject& project = state_.app_state.current_project.value();
    const std::filesystem::path relative_path = ReviewProposalRelativePath(proposal_id);
    if (ProjectTracksFile(project, relative_path)) {
        return core::ProjectService::RemoveTrackedFile(project, relative_path, true, error);
    }
    return state_.proposal_controller->manager.DeleteProposal(proposal_id, error);
}

void ReviewActions::CloseProposalPreviewIfOpen(const std::string& proposal_id) {
    if (!state_.proposal_controller->ClosePreviewIfOpen(proposal_id))
        return;
    RestoreBaseCanvas(state_);
}

void ReviewActions::BeginDeleteReviewItem(const core::reviews::ReviewItem& item) {
    const bool creator_active = state_.proposal_controller->creator_active;
    state_.review_controller->BeginDeleteReviewItem(item, creator_active);
    if (!item.proposal_id.has_value() && !creator_active)
        DeleteReviewItem(item);
}

bool ReviewActions::DeleteReviewItem(const core::reviews::ReviewItem& item) {
    const bool deleted = state_.review_controller->DeleteReviewItem(
        item,
        state_.proposal_controller->creator_active,
        state_.app_state.current_project.has_value(),
        [this](const std::string& proposal_id, std::string& error) {
            return DeleteProposalPatchFile(proposal_id, error);
        },
        [this](const std::string& proposal_id) { CloseProposalPreviewIfOpen(proposal_id); });
    if (deleted && state_.app_state.current_project.has_value()) {
        core::ProjectService::RefreshFileStatus(state_.app_state.current_project.value());
    }
    return deleted;
}

bool ReviewActions::DeleteProposalForReviewItem(const core::reviews::ReviewItem& item) {
    if (state_.proposal_controller->creator_active) {
        SetStatus(state_, "Save or discard the active proposal before deleting another proposal.");
        return false;
    }
    if (!item.proposal_id.has_value()) {
        SetStatus(state_, "This review comment has no proposed change to delete.");
        return false;
    }
    if (!state_.app_state.current_project.has_value()) {
        SetStatus(state_, "Open a project before deleting proposed changes.");
        return false;
    }

    core::AssuranceProject& project = state_.app_state.current_project.value();
    std::string error;
    if (!DeleteProposalPatchFile(item.proposal_id.value(), error)) {
        SetStatus(state_, "Proposal delete failed: " + error);
        return false;
    }

    if (!state_.review_controller->ClearProposal(item.id)) {
        SetStatus(state_, "Proposal deleted, but review link update failed.");
        return false;
    }
    CloseProposalPreviewIfOpen(item.proposal_id.value());

    core::ProjectService::RefreshFileStatus(project);
    SetStatus(state_, "Deleted proposed change " + item.proposal_id.value() + ".");
    return true;
}

bool ReviewActions::ResolveReviewItem(const core::reviews::ReviewItem& item, const std::string& updated_utc) {
    return state_.review_controller->ResolveReviewItem(
        item, state_.proposal_controller->creator_active, state_.app_state.current_project.has_value(), updated_utc);
}

} // namespace app::actions
