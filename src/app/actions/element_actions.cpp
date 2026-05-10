#include "app/actions/element_actions.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
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

    parser::AssuranceCase& ac = state_.app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg =
        state_.app_state.sacm_package.has_value() ? &state_.app_state.sacm_package.value() : nullptr;
    return state_.element_edit_controller->AddChildToSelected(ac, pkg, selected_id, kind);
}

bool ElementActions::AddTopGoal() {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return false;
    }

    parser::AssuranceCase& ac = state_.app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg =
        state_.app_state.sacm_package.has_value() ? &state_.app_state.sacm_package.value() : nullptr;
    return state_.element_edit_controller->AddTopGoal(ac, pkg);
}

void ElementActions::RemoveSelected(core::RemoveMode mode) {
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "No assurance case loaded.");
        return;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;

    parser::AssuranceCase& ac = state_.app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg =
        state_.app_state.sacm_package.has_value() ? &state_.app_state.sacm_package.value() : nullptr;
    if (!state_.element_edit_controller->RemoveSelected(ac, pkg, selected_id, mode))
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

    parser::AssuranceCase& model = state_.app_state.loaded_case.value();
    sacm::AssuranceCasePackage* package =
        state_.app_state.sacm_package.has_value() ? &state_.app_state.sacm_package.value() : nullptr;

    std::string error;
    bool changed = false;
    if (drop_mode == core::TreeDropMode::Before || drop_mode == core::TreeDropMode::After) {
        changed = core::ReorderSiblings(model,
                                        package,
                                        state_.current_tree,
                                        state_.tree_display_order,
                                        core::ReorderSiblingsCommand{dragged_id, target_id, drop_mode},
                                        error);
    } else {
        changed = core::MoveSubtree(
            model, package, state_.current_tree, core::MoveSubtreeCommand{dragged_id, target_id}, error);
    }

    if (!changed) {
        SetStatus(state_, "Tree move failed: " + error);
        return false;
    }

    state_.events.Emit(TreeDirtyEvent{});
    state_.events.Emit(SelectionChangedEvent{dragged_id, true});
    state_.events.Emit(DocumentDirtyEvent{});
    SetStatus(state_, drop_mode == core::TreeDropMode::AsChild ? "Moved " + dragged_id : "Reordered " + dragged_id);
    return true;
}

} // namespace app::actions