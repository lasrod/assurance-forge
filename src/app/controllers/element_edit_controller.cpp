#include "app/controllers/element_edit_controller.h"

#include "app/app_runtime_state.h"
#include "app/commands/dispatch.h"
#include "core/commands/element_commands.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <algorithm>
#include <cstddef>

namespace app::controllers {

namespace {

// Resolve the model/package the controller mutates. Both are owned by
// `state.app_state` and are always populated together as a side effect of
// a SACM parse. Returns false (and emits a status message) if either is
// missing — DispatchAuditedCommand has the same precondition, so this
// short-circuits before doing any work.
bool TryGetWorkingModel(AppRuntimeState& state,
                        const char* action_label,
                        AppEvents& events,
                        parser::AssuranceCase*& out_model,
                        sacm::AssuranceCasePackage*& out_package) {
    if (!state.app_state.loaded_case.has_value() || !state.app_state.sacm_package.has_value()) {
        events.Emit(StatusMessageEvent{std::string(action_label) + " failed: no SACM model loaded."});
        return false;
    }
    out_model   = &state.app_state.loaded_case.value();
    out_package = &state.app_state.sacm_package.value();
    return true;
}

} // namespace

ElementEditController::ElementEditController(AppEvents& events) : events_(events) {}

bool ElementEditController::AddChildToSelected(AppRuntimeState& state,
                                               const std::string& selected_id,
                                               core::NewElementKind kind) {
    if (selected_id.empty()) {
        events_.Emit(StatusMessageEvent{"No element selected."});
        return false;
    }
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Add", events_, model, package))
        return false;
    (void)model;
    (void)package;

    core::commands::CreateChildElementCommand cmd(selected_id, kind);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Add failed: " + outcome.error});
        return false;
    }

    const std::string& new_id = cmd.GeneratedId();
    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{new_id, true});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Added " + new_id});
    return true;
}

bool ElementEditController::AddTopGoal(AppRuntimeState& state) {
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Add", events_, model, package))
        return false;
    (void)model;
    (void)package;

    core::commands::CreateTopGoalCommand cmd;
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Add failed: " + outcome.error});
        return false;
    }

    const std::string& new_id = cmd.GeneratedId();
    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{new_id, true});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Added " + new_id});
    return true;
}

bool ElementEditController::AddChallenge(AppRuntimeState& state,
                                         const core::ArgumentTarget& target,
                                         core::ChallengeSourceType source_type) {
    if (target.id.empty()) {
        events_.Emit(StatusMessageEvent{"No challenge target selected."});
        return false;
    }
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Add challenge", events_, model, package))
        return false;
    (void)model;
    (void)package;

    core::commands::CreateChallengeCommand cmd(target, source_type);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Add challenge failed: " + outcome.error});
        return false;
    }

    const std::string& new_id = cmd.GeneratedId();
    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{new_id, true});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Added " + new_id});
    return true;
}

bool ElementEditController::RemoveSelected(AppRuntimeState& state,
                                           const std::string& selected_id,
                                           core::RemoveMode mode) {
    if (selected_id.empty()) {
        events_.Emit(StatusMessageEvent{"No element selected."});
        return false;
    }
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Remove", events_, model, package))
        return false;
    (void)package;

    auto planned = core::PlanRemoval(*model, selected_id, mode);
    if (planned.empty()) {
        events_.Emit(StatusMessageEvent{"Nothing to remove for this selection."});
        return false;
    }

    if (planned.size() == 1) {
        core::commands::RemoveElementCommand cmd(selected_id, mode);
        const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
        if (!outcome.success) {
            events_.Emit(StatusMessageEvent{"Remove failed: " + outcome.error});
            return false;
        }
        events_.Emit(TreeDirtyEvent{});
        events_.Emit(SelectionChangedEvent{});
        events_.Emit(DocumentDirtyEvent{});
        events_.Emit(StatusMessageEvent{"Removed " + selected_id});
        return true;
    }

    show_remove_confirm_ = true;
    pending_remove_id_   = selected_id;
    pending_remove_mode_ = mode;
    pending_remove_ids_.assign(planned.begin(), planned.end());
    std::sort(pending_remove_ids_.begin(), pending_remove_ids_.end());
    return true;
}

bool ElementEditController::ConfirmPendingRemoval(AppRuntimeState& state) {
    const std::string      id    = pending_remove_id_;
    const core::RemoveMode mode  = pending_remove_mode_;
    const size_t           count = pending_remove_ids_.size();
    CancelPendingRemoval();

    if (id.empty())
        return false;

    core::commands::RemoveElementCommand cmd(id, mode);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Remove failed: " + outcome.error});
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

bool ElementEditController::CommitElementTextEdit(AppRuntimeState& state,
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

    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Edit", events_, model, package))
        return false;

    // The panel's InputText already mutated the parser model in place as
    // the user typed. Revert it (and the SACM mirror) to the pre-edit value
    // so the dispatched command captures the correct `old_value` in its
    // audit payload — and so its Apply observes a real change rather than
    // new == old.
    std::string discarded;
    std::string revert_error;
    if (!core::SetElementTextField(*model, package, element_id, field, language, original_copy, discarded,
                                   revert_error)) {
        events_.Emit(StatusMessageEvent{"Edit failed: " + revert_error});
        return false;
    }

    core::commands::UpdateElementTextCommand cmd(element_id, field, language, new_copy);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Edit failed: " + outcome.error});
        // Best-effort: restore the value the user just typed so the UI does
        // not jump back to the pre-edit state on failure.
        std::string restore_error;
        core::SetElementTextField(*model, package, element_id, field, language, new_copy, discarded, restore_error);
        return false;
    }
    events_.Emit(DocumentDirtyEvent{});
    return true;
}

int ElementEditController::FlushPendingTextEdits(AppRuntimeState& state,
                                                 const std::vector<ui::PendingTextEdit>& pending) {
    int committed = 0;
    for (const ui::PendingTextEdit& edit : pending) {
        if (edit.element_id.empty() || edit.language.empty())
            continue;
        if (edit.original_value == edit.new_value)
            continue;
        if (CommitElementTextEdit(state, edit.element_id, edit.field_token, edit.language,
                                  edit.original_value, edit.new_value)) {
            ++committed;
        }
    }
    return committed;
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

} // namespace app::controllers