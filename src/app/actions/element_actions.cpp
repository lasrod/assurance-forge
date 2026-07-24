#include "app/actions/element_actions.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/commands/dispatch.h"
#include "core/commands/tree_commands.h"
#include "ui/ui_state.h"

namespace app::actions {
namespace {

void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

} // namespace

ElementActions::ElementActions(AppRuntimeState& state) : state_(state) {}

bool ElementActions::AddChildToSelected(core::NewElementKind kind) {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    return state_.element_edit_controller->AddChildToSelected(state_, selected_id, kind);
}

bool ElementActions::AddTopGoal() {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }
    return state_.element_edit_controller->AddTopGoal(state_);
}

bool ElementActions::AddChallengeToSelectedElement(core::ChallengeSourceType source_type) {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus(state_, "No element selected.");
        return false;
    }
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Element, selected_id};
    return state_.element_edit_controller->AddChallenge(state_, target, source_type);
}

bool ElementActions::AddChallengeToRelationship(const std::string& relationship_id,
                                                core::ChallengeSourceType source_type) {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }
    if (relationship_id.empty()) {
        SetStatus(state_, "No relationship selected.");
        return false;
    }
    core::ArgumentTarget target{core::ArgumentTarget::Kind::Relationship, relationship_id};
    return state_.element_edit_controller->AddChallenge(state_, target, source_type);
}

bool ElementActions::AddAcpToSelectedElement() {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }
    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus(state_, "No element selected.");
        return false;
    }
    return state_.acp_controller->AddElementAcp(state_, selected_id);
}

bool ElementActions::AddAcpToRelationship(const std::string& relationship_id) {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }
    return state_.acp_controller->AddRelationshipAcp(state_, relationship_id);
}

bool ElementActions::RemoveAcp(const std::string& acp_id) {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }
    return state_.acp_controller->RemoveAcp(state_, acp_id);
}

void ElementActions::RemoveSelected(core::RemoveMode mode) {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    if (!state_.element_edit_controller->RemoveSelected(state_, selected_id, mode))
        return;

    if (state_.element_edit_controller->ShouldShowRemoveConfirm()) {
        auto& s = ui::GetUiState();
        const auto& pending_ids = state_.element_edit_controller->PendingRemoveIds();
        s.marked_for_removal = {pending_ids.begin(), pending_ids.end()};
        s.center_on_marked = true;
    }
}

core::TreeDropValidationResult ElementActions::ValidateTreeDrop(const std::string& dragged_id,
                                                                const std::string& target_id,
                                                                core::TreeDropMode drop_mode) {
    if (!state_.app_state.loaded_case.has_value()) {
        core::TreeDropValidationResult result;
        result.reason = "No assurance case loaded.";
        return result;
    }

    if (!state_.tree_edit_index_valid) {
        state_.tree_edit_index = core::BuildTreeEditIndex(state_.app_state.loaded_case.value());
        state_.tree_edit_index_valid = true;
    }
    return core::ValidateTreeDrop(state_.tree_edit_index, state_.current_tree, dragged_id, target_id, drop_mode);
}

bool ElementActions::PerformTreeDrop(const std::string& dragged_id,
                                     const std::string& target_id,
                                     core::TreeDropMode drop_mode) {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }

    // Route the drop through the audited command bus so it is recorded (replayable,
    // no verify divergence) AND library-primary (the library document is mutated and
    // the live views re-derive from it). The command owns the mutation -- the same
    // `core::ReorderSiblings` / `core::MoveSubtree` this used to call directly on the
    // loaded case + package, which internally re-validate the drop (preserving the
    // pre-check behavior). This was the last bus-bypassing writer of `sacm_package`.
    if (drop_mode == core::TreeDropMode::Before || drop_mode == core::TreeDropMode::After) {
        core::commands::ReorderSiblingsCommand command(dragged_id, target_id, drop_mode);
        const app::commands::DispatchOutcome outcome =
            app::commands::DispatchAuditedCommand(state_, command);
        if (!outcome.success) {
            SetStatus(state_,
                      outcome.error.empty() ? "Tree move failed." : "Tree move failed: " + outcome.error);
            return false;
        }
        // Refresh the transient render-order override immediately so the reorder is
        // visible before the frame-boundary re-derive rebuilds the tree from the
        // (now reordered) SACM.
        for (const auto& [parent_id, order] : command.ReorderedOrder().children_by_parent)
            state_.tree_display_order.children_by_parent[parent_id] = order;
    } else {
        core::commands::MoveSubtreeCommand command(dragged_id, target_id);
        const app::commands::DispatchOutcome outcome =
            app::commands::DispatchAuditedCommand(state_, command);
        if (!outcome.success) {
            SetStatus(state_,
                      outcome.error.empty() ? "Tree move failed." : "Tree move failed: " + outcome.error);
            return false;
        }
    }

    state_.events.Emit(TreeDirtyEvent{});
    state_.events.Emit(SelectionChangedEvent{dragged_id, true});
    state_.events.Emit(DocumentDirtyEvent{});
    SetStatus(state_, drop_mode == core::TreeDropMode::AsChild ? "Moved " + dragged_id : "Reordered " + dragged_id);
    return true;
}

} // namespace app::actions