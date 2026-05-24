#include "app/controllers/element_edit_controller.h"

#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"

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
    if (command_bus_ && package) {
        core::commands::CreateChildElementCommand cmd(selected_id, kind);
        core::commands::CommandContext ctx{model, *package};
        const auto result = command_bus_->Execute(cmd, ctx, {});
        EmitAutosaveStatus(result);
        if (!result.success) {
            events_.Emit(StatusMessageEvent{"Add failed: " + result.error});
            return false;
        }
        new_id = cmd.GeneratedId();
    } else {
        std::string error;
        if (!core::AddChildElement(model, package, selected_id, kind, new_id, error)) {
            events_.Emit(StatusMessageEvent{"Add failed: " + error});
            return false;
        }
    }

    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{new_id, true});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Added " + new_id});
    return true;
}

bool ElementEditController::AddTopGoal(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package) {
    std::string new_id;
    if (command_bus_ && package) {
        core::commands::CreateTopGoalCommand cmd;
        core::commands::CommandContext ctx{model, *package};
        const auto result = command_bus_->Execute(cmd, ctx, {});
        EmitAutosaveStatus(result);
        if (!result.success) {
            events_.Emit(StatusMessageEvent{"Add failed: " + result.error});
            return false;
        }
        new_id = cmd.GeneratedId();
    } else {
        std::string error;
        if (!core::AddTopGoal(model, package, new_id, error)) {
            events_.Emit(StatusMessageEvent{"Add failed: " + error});
            return false;
        }
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
        if (command_bus_ && package) {
            core::commands::RemoveElementCommand cmd(selected_id, mode);
            core::commands::CommandContext ctx{model, *package};
            const auto result = command_bus_->Execute(cmd, ctx, {});
            EmitAutosaveStatus(result);
            if (!result.success) {
                events_.Emit(StatusMessageEvent{"Remove failed: " + result.error});
                return false;
            }
        } else {
            std::string error;
            if (!core::RemoveElement(model, package, selected_id, mode, error)) {
                events_.Emit(StatusMessageEvent{"Remove failed: " + error});
                return false;
            }
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

    if (id.empty())
        return false;

    if (command_bus_ && package) {
        core::commands::RemoveElementCommand cmd(id, mode);
        core::commands::CommandContext ctx{model, *package};
        const auto result = command_bus_->Execute(cmd, ctx, {});
        EmitAutosaveStatus(result);
        if (!result.success) {
            events_.Emit(StatusMessageEvent{"Remove failed: " + result.error});
            return false;
        }
    } else {
        std::string error;
        if (!core::RemoveElement(model, package, id, mode, error)) {
            events_.Emit(StatusMessageEvent{"Remove failed: " + error});
            return false;
        }
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

bool ElementEditController::CommitElementTextEdit(parser::AssuranceCase& model,
                                                  sacm::AssuranceCasePackage* package,
                                                  const std::string& element_id,
                                                  const std::string& field_token,
                                                  const std::string& language,
                                                  const std::string& original_value,
                                                  const std::string& new_value) {
    // The panel passes `new_value` as a reference to the parser element's
    // own string (ImGui's per-keystroke binding writes directly into the
    // model). Below we revert that string to `original_value` before
    // executing the audited command — that revert would mutate the very
    // memory `new_value` aliases, making the subsequent command see
    // new == old and emit a no-op transaction. Snapshot both inputs into
    // independent locals before doing anything else.
    const std::string original_copy = original_value;
    const std::string new_copy      = new_value;

    if (element_id.empty() || language.empty())
        return false;
    if (original_copy == new_copy)
        return false;
    core::ElementTextField field;
    if (!core::ElementTextFieldFromToken(field_token, field)) {
        events_.Emit(StatusMessageEvent{"Edit failed: unknown field '" + field_token + "'."});
        return false;
    }

    if (command_bus_ && package) {
        // The panel's InputText already mutated the parser model in place
        // as the user typed. Revert it (and the SACM mirror) to the
        // pre-edit value so the audited command captures the correct
        // `old_value` before writing the final state.
        std::string discarded;
        std::string revert_error;
        if (!core::SetElementTextField(model, package, element_id, field, language, original_copy, discarded,
                                       revert_error)) {
            events_.Emit(StatusMessageEvent{"Edit failed: " + revert_error});
            return false;
        }
        core::commands::UpdateElementTextCommand cmd(element_id, field, language, new_copy);
        core::commands::CommandContext ctx{model, *package};
        const auto result = command_bus_->Execute(cmd, ctx, {});
        EmitAutosaveStatus(result);
        if (!result.success) {
            events_.Emit(StatusMessageEvent{"Edit failed: " + result.error});
            // Best-effort: restore the value the user just typed so the UI
            // does not jump back to the pre-edit state on failure.
            std::string restore_error;
            core::SetElementTextField(model, package, element_id, field, language, new_copy, discarded,
                                      restore_error);
            return false;
        }
        events_.Emit(DocumentDirtyEvent{});
        return true;
    }

    // No audit store wired (e.g., standalone-file mode). Parser was already
    // mutated by the InputText binding; just ensure SACM mirrors it.
    std::string discarded;
    std::string error;
    if (!core::SetElementTextField(model, package, element_id, field, language, new_copy, discarded, error)) {
        events_.Emit(StatusMessageEvent{"Edit failed: " + error});
        return false;
    }
    events_.Emit(DocumentDirtyEvent{});
    return true;
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

void ElementEditController::EmitAutosaveStatus(const core::commands::CommandResult& result) {
    // CommandBus contract: when the audited write to disk fails after the
    // log entry was committed, `error` begins with "Autosave failed". When
    // the manifest update fails but log and SACM are consistent, success is
    // true with a non-empty error. Both are user-visible. Plain success
    // (no error) clears any stale banner.
    if (!result.error.empty()) {
        events_.Emit(AutosaveFailedEvent{result.error});
    } else if (result.success) {
        events_.Emit(AutosaveFailedEvent{std::string{}});
    }
}

} // namespace app::controllers