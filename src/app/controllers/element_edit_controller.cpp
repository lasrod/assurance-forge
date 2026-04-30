#include "app/controllers/element_edit_controller.h"

#include <algorithm>
#include <cstddef>

namespace app::controllers {

ElementEditController::ElementEditController(AppEvents& events) : events_(events) {}

bool ElementEditController::AddChildToSelected(parser::AssuranceCase& model,
                                               sacm::AssuranceCasePackage* package,
                                               const std::string& selected_id,
                                               core::NewElementKind kind) {
    if (selected_id.empty()) {
        events_.Emit(StatusMessageEvent{"No element selected."});
        return false;
    }

    std::string new_id;
    std::string error;
    if (!core::AddChildElement(model, package, selected_id, kind, new_id, error)) {
        events_.Emit(StatusMessageEvent{"Add failed: " + error});
        return false;
    }

    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{new_id, true});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Added " + new_id});
    return true;
}

bool ElementEditController::AddTopGoal(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package) {
    std::string new_id;
    std::string error;
    if (!core::AddTopGoal(model, package, new_id, error)) {
        events_.Emit(StatusMessageEvent{"Add failed: " + error});
        return false;
    }

    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{new_id, true});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Added " + new_id});
    return true;
}

bool ElementEditController::RemoveSelected(parser::AssuranceCase& model,
                                           sacm::AssuranceCasePackage* package,
                                           const std::string& selected_id,
                                           core::RemoveMode mode) {
    if (selected_id.empty()) {
        events_.Emit(StatusMessageEvent{"No element selected."});
        return false;
    }

    auto planned = core::PlanRemoval(model, selected_id, mode);
    if (planned.empty()) {
        events_.Emit(StatusMessageEvent{"Nothing to remove for this selection."});
        return false;
    }

    if (planned.size() == 1) {
        std::string error;
        if (!core::RemoveElement(model, package, selected_id, mode, error)) {
            events_.Emit(StatusMessageEvent{"Remove failed: " + error});
            return false;
        }
        events_.Emit(TreeDirtyEvent{});
        events_.Emit(SelectionChangedEvent{});
        events_.Emit(DocumentDirtyEvent{});
        events_.Emit(StatusMessageEvent{"Removed " + selected_id});
        return true;
    }

    show_remove_confirm_ = true;
    pending_remove_id_ = selected_id;
    pending_remove_mode_ = mode;
    pending_remove_ids_.assign(planned.begin(), planned.end());
    std::sort(pending_remove_ids_.begin(), pending_remove_ids_.end());
    return true;
}

bool ElementEditController::ConfirmPendingRemoval(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package) {
    const std::string id = pending_remove_id_;
    const core::RemoveMode mode = pending_remove_mode_;
    const size_t count = pending_remove_ids_.size();
    CancelPendingRemoval();

    if (id.empty()) return false;

    std::string error;
    if (!core::RemoveElement(model, package, id, mode, error)) {
        events_.Emit(StatusMessageEvent{"Remove failed: " + error});
        return false;
    }

    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Removed " + std::to_string(count) + " element" + (count == 1 ? "" : "s")});
    return true;
}

void ElementEditController::CancelPendingRemoval() {
    show_remove_confirm_ = false;
    pending_remove_id_.clear();
    pending_remove_ids_.clear();
}

bool ElementEditController::ShouldShowRemoveConfirm() const {
    return show_remove_confirm_;
}

const std::string& ElementEditController::PendingRemoveId() const {
    return pending_remove_id_;
}

core::RemoveMode ElementEditController::PendingRemoveMode() const {
    return pending_remove_mode_;
}

const std::vector<std::string>& ElementEditController::PendingRemoveIds() const {
    return pending_remove_ids_;
}

}  // namespace app::controllers