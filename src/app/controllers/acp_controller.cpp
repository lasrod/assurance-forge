#include "app/controllers/acp_controller.h"

#include "app/acp_problem_sync.h"
#include "app/app_runtime_state.h"
#include "app/commands/dispatch.h"
#include "core/acp/acp_editing.h"
#include "core/commands/acp_commands.h"
#include "ui/ui_state.h"

namespace app::controllers {

AcpController::AcpController(AppEvents& events, core::ProblemsManager& problems_manager,
                            std::function<void()> on_edit_applied)
    : events_(events), problems_manager_(problems_manager),
      on_edit_applied_(std::move(on_edit_applied)) {}

void AcpController::NotifyEditApplied() {
    if (on_edit_applied_) {
        on_edit_applied_();
    }
}

void AcpController::SyncProblems(const parser::AssuranceCase& model, const sacm::AssuranceCasePackage* package) {
    app::SyncAcpProblems(problems_manager_, &model, package);
}

bool AcpController::DispatchAddAcp(AppRuntimeState& state, const std::string& target_kind,
                                   const std::string& target_id) {
    core::commands::AddAcpCommand        command(target_kind, target_id);
    const app::commands::DispatchOutcome outcome = app::commands::DispatchAuditedCommand(state, command);
    if (!outcome.success) {
        // An empty error is a benign no-op (nothing to record); only surface a
        // real failure, matching the pre-bus controller's silent no-op.
        if (!outcome.error.empty())
            events_.Emit(StatusMessageEvent{"Added ACP failed: " + outcome.error});
        return false;
    }
    const std::string& acp_id = command.GeneratedAcpId();
    ui::UiState&       ui_state = ui::GetUiState();
    ui_state.selected_acp_id = acp_id;
    ui_state.selected_element_id.clear();
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Added ACP " + acp_id});
    // The command bus + frame-boundary re-derive refresh the model; ACP problems
    // re-sync from the fresh model when this dirty flag is serviced next frame
    // (an immediate sync here would read the momentarily-stale flipped views).
    state.problems_dirty.acp = true;
    return true;
}

bool AcpController::AddElementAcp(AppRuntimeState& state, const std::string& element_id) {
    return DispatchAddAcp(state, "element", element_id);
}

bool AcpController::AddRelationshipAcp(AppRuntimeState& state, const std::string& relationship_id) {
    return DispatchAddAcp(state, "relationship", relationship_id);
}

bool AcpController::RemoveAcp(AppRuntimeState& state, const std::string& acp_id) {
    core::commands::RemoveAcpCommand     command(acp_id);
    const app::commands::DispatchOutcome outcome = app::commands::DispatchAuditedCommand(state, command);
    if (!outcome.success) {
        if (!outcome.error.empty())
            events_.Emit(StatusMessageEvent{"Remove ACP failed: " + outcome.error});
        return false;
    }
    ui::UiState& ui_state = ui::GetUiState();
    if (ui_state.selected_acp_id == acp_id)
        ui_state.selected_acp_id.clear();
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Removed " + acp_id});
    state.problems_dirty.acp = true;
    return true;
}

bool AcpController::UpsertAcp(AppRuntimeState& state, const parser::AcpRecord& acp) {
    core::commands::UpsertAcpCommand     command(acp);
    const app::commands::DispatchOutcome outcome = app::commands::DispatchAuditedCommand(state, command);
    if (!outcome.success) {
        if (!outcome.error.empty())
            events_.Emit(StatusMessageEvent{"Update ACP failed: " + outcome.error});
        return false;
    }
    events_.Emit(DocumentDirtyEvent{});
    state.problems_dirty.acp = true;
    return true;
}

bool AcpController::CreateConfidenceArgumentTreeForAcp(parser::AssuranceCase& model,
                                                       sacm::AssuranceCasePackage* package,
                                                       const std::string& acp_id) {
    const core::acp::AcpEditResult result = core::acp::CreateConfidenceArgumentTreeForAcp(model, package, acp_id);
    if (!result.error.empty()) {
        events_.Emit(StatusMessageEvent{"Create confidence argument tree failed: " + result.error});
        return false;
    }
    if (!result.changed)
        return false;

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.selected_acp_id.clear();
    ui_state.selected_element_id = result.top_goal_id;
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();
    ui_state.center_on_selection = true;
    ui_state.center_view = ui::CenterView::GsnCanvas;
    events_.Emit(TreeDirtyEvent{});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(ProjectFilesChangedEvent{});
    events_.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
    events_.Emit(ArgumentPackageCanvasRequestEvent{result.argument_package_id, {}, "Confidence argument", result.top_goal_id});
    events_.Emit(
        StatusMessageEvent{"Created confidence argument tree " + result.argument_package_id + " for " + result.acp_id});
    SyncProblems(model, package);
    NotifyEditApplied();
    return true;
}

bool AcpController::OpenConfidenceArgumentTreeForAcp(const parser::AssuranceCase& model, const std::string& acp_id) {
    const parser::AcpRecord* acp = core::acp::FindAcp(model, acp_id);
    if (!acp) {
        events_.Emit(StatusMessageEvent{"Open confidence argument tree failed: ACP was not found."});
        return false;
    }
    if (acp->resolution_kind != "topGoalReference" || acp->argument_package_id.empty() || acp->top_goal_id.empty()) {
        events_.Emit(StatusMessageEvent{"Open confidence argument tree failed: ACP is not linked to a tree."});
        return false;
    }
    const std::string title = (acp->name.empty() || acp->name == acp->id) ? "Confidence argument for " + acp->id
                                                                          : acp->id + ": " + acp->name;
    events_.Emit(ArgumentPackageCanvasRequestEvent{acp->argument_package_id, {}, title, acp->top_goal_id});
    events_.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
    return true;
}

} // namespace app::controllers