#include "app/controllers/acp_controller.h"

#include "app/acp_problem_sync.h"
#include "core/acp/acp_editing.h"
#include "ui/ui_state.h"

namespace app::controllers {

AcpController::AcpController(AppEvents& events, core::ProblemsManager& problems_manager)
    : events_(events), problems_manager_(problems_manager) {}

void AcpController::SyncProblems(const parser::AssuranceCase& model, const sacm::AssuranceCasePackage* package) {
    app::SyncAcpProblems(problems_manager_, &model, package);
}

bool AcpController::HandleResult(const char* action, const core::acp::AcpEditResult& result) {
    if (!result.error.empty()) {
        events_.Emit(StatusMessageEvent{std::string(action) + " failed: " + result.error});
        return false;
    }
    if (!result.changed)
        return false;

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.selected_acp_id = result.acp_id;
    ui_state.selected_element_id.clear();
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{std::string(action) + " " + result.acp_id});
    return true;
}

bool AcpController::AddElementAcp(parser::AssuranceCase& model,
                                  sacm::AssuranceCasePackage* package,
                                  const std::string& element_id) {
    const bool handled = HandleResult("Added ACP", core::acp::AddAcp(model, package, "element", element_id));
    if (handled)
        SyncProblems(model, package);
    return handled;
}

bool AcpController::AddRelationshipAcp(parser::AssuranceCase& model,
                                       sacm::AssuranceCasePackage* package,
                                       const std::string& relationship_id) {
    const bool handled = HandleResult("Added ACP", core::acp::AddAcp(model, package, "relationship", relationship_id));
    if (handled)
        SyncProblems(model, package);
    return handled;
}

bool AcpController::RemoveAcp(parser::AssuranceCase& model,
                              sacm::AssuranceCasePackage* package,
                              const std::string& acp_id) {
    const core::acp::AcpEditResult result = core::acp::RemoveAcp(model, package, acp_id);
    if (!result.error.empty()) {
        events_.Emit(StatusMessageEvent{"Remove ACP failed: " + result.error});
        return false;
    }
    if (!result.changed)
        return false;
    ui::UiState& ui_state = ui::GetUiState();
    if (ui_state.selected_acp_id == acp_id)
        ui_state.selected_acp_id.clear();
    ui_state.selected_relationship_id.clear();
    ui_state.selected_relationship_edge_key.clear();
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Removed " + acp_id});
    SyncProblems(model, package);
    return true;
}

bool AcpController::UpsertAcp(parser::AssuranceCase& model,
                              sacm::AssuranceCasePackage* package,
                              const parser::AcpRecord& acp) {
    const core::acp::AcpEditResult result = core::acp::UpsertAcp(model, package, acp);
    if (!result.error.empty()) {
        events_.Emit(StatusMessageEvent{"Update ACP failed: " + result.error});
        return false;
    }
    if (!result.changed)
        return false;
    events_.Emit(DocumentDirtyEvent{});
    SyncProblems(model, package);
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