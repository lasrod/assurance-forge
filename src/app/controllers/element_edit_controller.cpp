#include "app/controllers/element_edit_controller.h"

#include "app/app_runtime_state.h"
#include "app/commands/dispatch.h"
#include "core/commands/element_commands.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"
#include "sacm_adapter/document_edit.h"
#include "ui/ui_state.h"

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
    if (!state.app_state.loaded_case.has_value() || !state.app_state.has_projected_package()) {
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

bool ElementEditController::RemoveRelationship(AppRuntimeState& state, const std::string& relationship_id) {
    if (relationship_id.empty()) {
        events_.Emit(StatusMessageEvent{"No relationship selected."});
        return false;
    }
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Remove relationship", events_, model, package))
        return false;

    core::commands::RemoveRelationshipCommand cmd(relationship_id);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Remove relationship failed: " + outcome.error});
        return false;
    }

    // The relationship is gone, so a selection pointing at it would describe
    // something that no longer exists.
    ui::GetUiState().selected_relationship_id.clear();
    ui::GetUiState().selected_relationship_edge_key.clear();
    events_.Emit(TreeDirtyEvent{});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Removed relationship " + relationship_id +
                                    ". Its elements were kept; any left unconnected show as orphans."});
    return true;
}

bool ElementEditController::DropRelationshipReference(AppRuntimeState& state,
                                                      const std::string& relationship_id,
                                                      const std::string& reference) {
    if (relationship_id.empty() || reference.empty()) {
        events_.Emit(StatusMessageEvent{"No broken reference selected."});
        return false;
    }
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Drop reference", events_, model, package))
        return false;

    core::commands::DropRelationshipReferenceCommand cmd(relationship_id, reference);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Drop reference failed: " + outcome.error});
        return false;
    }

    events_.Emit(TreeDirtyEvent{});
    events_.Emit(DocumentDirtyEvent{});
    // Say which of the two outcomes happened. Scrubbing the last source out of a
    // relationship takes the relationship with it, and a user who expected to
    // lose only a broken reference has to be told.
    if (cmd.RemovedRelationship()) {
        ui::GetUiState().selected_relationship_id.clear();
        ui::GetUiState().selected_relationship_edge_key.clear();
        events_.Emit(StatusMessageEvent{"Dropped " + reference + ", which left relationship " +
                                        relationship_id + " with nothing to relate, so it was removed too."});
    } else {
        events_.Emit(StatusMessageEvent{"Dropped the broken reference " + reference + " from " +
                                        relationship_id + "."});
    }
    return true;
}

bool ElementEditController::MoveStrategyToReasoning(AppRuntimeState& state,
                                                    const std::string& relationship_id,
                                                    const std::string& strategy_id) {
    if (relationship_id.empty() || strategy_id.empty()) {
        events_.Emit(StatusMessageEvent{"No strategy selected."});
        return false;
    }
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Move strategy", events_, model, package))
        return false;

    core::commands::MoveStrategyToReasoningCommand cmd(relationship_id, strategy_id);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Move strategy failed: " + outcome.error});
        return false;
    }

    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{strategy_id, true});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Moved " + strategy_id + " into the reasoning of " + relationship_id + "."});
    return true;
}

bool ElementEditController::SetElementUndeveloped(AppRuntimeState& state,
                                                  const std::string& element_id,
                                                  bool undeveloped) {
    if (element_id.empty()) {
        events_.Emit(StatusMessageEvent{"No element selected."});
        return false;
    }
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Set undeveloped", events_, model, package))
        return false;

    core::commands::SetElementUndevelopedCommand cmd(element_id, undeveloped);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Set undeveloped failed: " + outcome.error});
        return false;
    }
    if (cmd.WasNoOp())
        return true;

    events_.Emit(TreeDirtyEvent{});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{undeveloped ? "Marked " + element_id + " undeveloped."
                                                : "Cleared the undeveloped decorator on " + element_id + "."});
    return true;
}

bool ElementEditController::RenumberGsnIdentifier(AppRuntimeState& state, const std::string& element_id) {
    if (element_id.empty()) {
        events_.Emit(StatusMessageEvent{"No element selected."});
        return false;
    }
    parser::AssuranceCase*      model   = nullptr;
    sacm::AssuranceCasePackage* package = nullptr;
    if (!TryGetWorkingModel(state, "Renumber", events_, model, package))
        return false;

    const std::string next = core::NextFreeGsnIdentifier(*model, element_id);
    if (next.empty()) {
        events_.Emit(StatusMessageEvent{"Could not find a free GSN identifier for " + element_id + "."});
        return false;
    }

    core::commands::UpdateGsnIdentifierCommand cmd(element_id, next);
    const auto outcome = app::commands::DispatchAuditedCommand(state, cmd);
    if (!outcome.success) {
        events_.Emit(StatusMessageEvent{"Renumber failed: " + outcome.error});
        return false;
    }

    events_.Emit(TreeDirtyEvent{});
    events_.Emit(SelectionChangedEvent{element_id, true});
    events_.Emit(DocumentDirtyEvent{});
    events_.Emit(StatusMessageEvent{"Renumbered " + cmd.OldIdentifier() + " to " + next + "."});
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

    std::vector<std::string> planned_ids(planned.begin(), planned.end());
    std::sort(planned_ids.begin(), planned_ids.end());
    BuildRemovalPreview(state, planned_ids, mode);

    // Confirm whenever the removal reaches past what the user picked: several
    // planned elements, or the library reporting that something else goes with
    // them. A leaf with no consequences still deletes immediately — putting a
    // dialog in front of every single delete teaches the user to dismiss it
    // unread, which costs more than it protects.
    if (planned.size() == 1 && pending_remove_consequences_.empty()) {
        CancelPendingRemoval();
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
    pending_remove_ids_  = std::move(planned_ids);
    return true;
}

void ElementEditController::BuildRemovalPreview(AppRuntimeState& state,
                                                const std::vector<std::string>& planned_ids,
                                                core::RemoveMode mode) {
    pending_remove_targets_.clear();
    pending_remove_consequences_.clear();
    pending_remove_warnings_.clear();
    pending_remove_preview_available_ = false;

    // See the header: NodeOnly reparents rather than deletes, and the library
    // cannot express a retarget. Previewing it as a set of deletes would report
    // a child's inference as removed when it survives. No preview is better
    // than a wrong one.
    if (mode == core::RemoveMode::NodeOnly)
        return;

    // The library document is the source of truth for what a delete implies
    // (SACM23-INT-002). Without one there is nothing to ask, and the modal
    // says so rather than presenting the plan as if the library had confirmed
    // it.
    if (state.app_state.library_document == nullptr)
        return;

    const sacm_adapter::DeletePreview preview =
        sacm_adapter::preview_delete_elements(*state.app_state.library_document, planned_ids);
    if (!preview.supported)
        return;

    const auto to_effect = [](const sacm_adapter::DeleteEffect& effect) {
        return RemovalEffect{
            .element_id     = effect.element_id,
            .kind           = effect.kind,
            .name           = effect.name,
            .is_relationship = effect.is_relationship,
            .deleted        = effect.deleted,
        };
    };
    for (const sacm_adapter::DeleteEffect& effect : preview.requested)
        pending_remove_targets_.push_back(to_effect(effect));
    for (const sacm_adapter::DeleteEffect& effect : preview.consequential)
        pending_remove_consequences_.push_back(to_effect(effect));
    for (const sacm_adapter::LoadDiagnostic& diagnostic : preview.diagnostics)
        pending_remove_warnings_.push_back(diagnostic.code + ": " + diagnostic.message);

    AppendAcpConsequences(state);

    // A preview in which nothing the user selected resolved is not a preview
    // that found no consequences — it is no preview at all. Reporting it as
    // available renders an empty consequence list, which reads as "nothing
    // else is affected".
    pending_remove_preview_available_ = !pending_remove_targets_.empty();
}

// Assurance Claim Points are stored as `assuranceForge.acp.*` TaggedValues on
// the element they annotate, and the seam filters utility elements out of the
// effect list (they are attachments of an owner already listed). That filter is
// right for descriptions and notes and wrong for ACPs: an ACP is a first-class
// record with its own panel, and when its owner is a *consequential* deletion
// the user never selected it, nothing on screen would say the ACP is going.
void ElementEditController::AppendAcpConsequences(AppRuntimeState& state) {
    if (!state.app_state.loaded_case.has_value())
        return;

    std::vector<std::string> doomed;
    for (const std::vector<RemovalEffect>* bucket :
         {&pending_remove_targets_, &pending_remove_consequences_}) {
        for (const RemovalEffect& effect : *bucket) {
            if (effect.deleted)
                doomed.push_back(effect.element_id);
        }
    }

    for (const core::AcpRecord& acp : state.app_state.loaded_case->acps) {
        if (std::find(doomed.begin(), doomed.end(), acp.target_id) == doomed.end())
            continue;
        pending_remove_consequences_.push_back(RemovalEffect{
            .element_id      = acp.id,
            .kind            = "AssuranceClaimPoint",
            .name            = acp.name,
            .is_relationship = false,
            .deleted         = true,
        });
    }
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
    pending_remove_targets_.clear();
    pending_remove_consequences_.clear();
    pending_remove_warnings_.clear();
    pending_remove_preview_available_ = false;
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

    if (element_id.empty())
        return false;
    if (original_copy == new_copy)
        return false;

    if (field_token == "gsn_identifier") {
        parser::AssuranceCase* model = nullptr;
        sacm::AssuranceCasePackage* package = nullptr;
        if (!TryGetWorkingModel(state, "Edit", events_, model, package))
            return false;

        // Revert the parser model only — pass no package. The panel's in-place
        // edit never touched the SACM package, so the package still holds the
        // pre-edit state; reverting it too would upsert the vendor TaggedValue
        // on an element that may not carry one yet, leaving a tag behind in the
        // document if the audited command below then fails. The command applies
        // the change to both, so the two stay in step either way.
        std::string discarded;
        std::string revert_error;
        if (!core::SetGsnIdentifier(*model, nullptr, element_id, original_copy, discarded, revert_error)) {
            events_.Emit(StatusMessageEvent{"Edit failed: " + revert_error});
            return false;
        }

        core::commands::UpdateGsnIdentifierCommand command(element_id, new_copy);
        const auto outcome = app::commands::DispatchAuditedCommand(state, command);
        if (!outcome.success) {
            // Deliberately no best-effort restore of the typed value here: a
            // rejected identifier is invalid (empty, padded, or a duplicate),
            // so leaving it in the field would show an identifier the document
            // does not have. Model and package both hold the pre-edit value.
            events_.Emit(StatusMessageEvent{"Edit failed: " + outcome.error});
            return false;
        }
        events_.Emit(DocumentDirtyEvent{});
        return true;
    }

    if (language.empty())
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

const std::vector<ElementEditController::RemovalEffect>&
ElementEditController::PendingRemoveTargets() const {
    return pending_remove_targets_;
}

const std::vector<ElementEditController::RemovalEffect>&
ElementEditController::PendingRemoveConsequences() const {
    return pending_remove_consequences_;
}

const std::vector<std::string>& ElementEditController::PendingRemoveWarnings() const {
    return pending_remove_warnings_;
}

bool ElementEditController::PendingRemovePreviewAvailable() const {
    return pending_remove_preview_available_;
}

} // namespace app::controllers
