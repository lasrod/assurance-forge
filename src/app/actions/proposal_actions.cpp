#include "app/actions/proposal_actions.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/proposal_ui_state.h"
#include "app/project_workflow.h"
#include "app/sacm_argument_sync.h"
#include "core/project_service.h"
#include "core/reviews/review_proposal_factory.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "core/string_utils.h"
#include "core/time_utils.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace app::actions {
namespace {

using core::NowUtcString;
using core::TrimWhitespace;
using core::reviews::BuildDraftReviewProposal;

struct ElementTextTarget {
    std::string field;
    std::string current_text;
};

void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

core::reviews::PatchOperationType CreateOperationFor(core::NewElementKind kind) {
    switch (kind) {
    case core::NewElementKind::Goal:
        return core::reviews::PatchOperationType::CreateClaim;
    case core::NewElementKind::Strategy:
        return core::reviews::PatchOperationType::CreateStrategy;
    case core::NewElementKind::Solution:
        return core::reviews::PatchOperationType::CreateSolution;
    case core::NewElementKind::Context:
        return core::reviews::PatchOperationType::CreateContext;
    case core::NewElementKind::Assumption:
        return core::reviews::PatchOperationType::CreateAssumption;
    case core::NewElementKind::Justification:
        return core::reviews::PatchOperationType::CreateJustification;
    }
    return core::reviews::PatchOperationType::CreateClaim;
}

const char* CreateRefPrefixFor(core::NewElementKind kind) {
    switch (kind) {
    case core::NewElementKind::Goal:
        return "$new_claim_";
    case core::NewElementKind::Strategy:
        return "$new_strategy_";
    case core::NewElementKind::Solution:
        return "$new_solution_";
    case core::NewElementKind::Context:
        return "$new_context_";
    case core::NewElementKind::Assumption:
        return "$new_assumption_";
    case core::NewElementKind::Justification:
        return "$new_justification_";
    }
    return "$new_element_";
}

bool IsContextLike(core::NewElementKind kind) {
    return kind == core::NewElementKind::Context || kind == core::NewElementKind::Assumption ||
           kind == core::NewElementKind::Justification;
}

ElementTextTarget TextTargetFor(const parser::SacmElement& element) {
    if (!element.content.empty())
        return {"content", element.content};
    if (!element.description.empty())
        return {"description", element.description};
    return {"name", element.name};
}

const char* RemoveModeField(core::RemoveMode mode) {
    return mode == core::RemoveMode::NodeAndDescendants ? core::reviews::kReviewProposalRemoveModeNodeAndDescendants
                                                        : core::reviews::kReviewProposalRemoveModeNodeOnly;
}

std::string GenerateCreateRef(const core::reviews::ReviewProposal& proposal, core::NewElementKind kind) {
    std::unordered_set<std::string> used;
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.create_ref.has_value())
            used.insert(operation.create_ref.value());
    }
    const std::string prefix = CreateRefPrefixFor(kind);
    for (int index = 1; index < 100000; ++index) {
        std::string candidate = prefix + std::to_string(index);
        if (used.count(candidate) == 0)
            return candidate;
    }
    return prefix + std::to_string(used.size() + 1);
}

core::reviews::ElementRef ExistingElementRef(const std::string& id) {
    return core::reviews::ElementRef{id, std::nullopt};
}

core::reviews::ElementRef CreatedElementRef(const std::string& create_ref) {
    return core::reviews::ElementRef{std::nullopt, create_ref};
}

bool SameElementRef(const core::reviews::ElementRef& lhs, const core::reviews::ElementRef& rhs) {
    return lhs.existing_id == rhs.existing_id && lhs.create_ref == rhs.create_ref;
}

std::optional<core::reviews::ElementRef>
ProposalRefForPreviewId(const std::string& preview_id, const std::map<std::string, std::string>& generated_ids) {
    for (const auto& generated : generated_ids) {
        if (generated.second == preview_id)
            return CreatedElementRef(generated.first);
    }
    if (!preview_id.empty())
        return ExistingElementRef(preview_id);
    return std::nullopt;
}

std::string PreviewIdForProposalRef(const core::reviews::ElementRef& ref,
                                    const std::map<std::string, std::string>& generated_ids) {
    if (ref.existing_id.has_value())
        return ref.existing_id.value();
    if (ref.create_ref.has_value()) {
        auto found = generated_ids.find(ref.create_ref.value());
        if (found != generated_ids.end())
            return found->second;
    }
    return {};
}

void TrackAffectedExistingElement(core::reviews::ReviewProposal& proposal,
                                  const parser::AssuranceCase& base_model,
                                  const std::string& element_id) {
    if (element_id.empty())
        return;
    if (std::find(proposal.affected_existing_element_ids.begin(),
                  proposal.affected_existing_element_ids.end(),
                  element_id) == proposal.affected_existing_element_ids.end()) {
        proposal.affected_existing_element_ids.push_back(element_id);
    }
    if (proposal.base_element_hashes.count(element_id) == 0) {
        if (const parser::SacmElement* element = parser::FindElementByIdOrGidValue(base_model, element_id)) {
            proposal.base_element_hashes[element_id] = core::reviews::ComputeElementSemanticHash(*element);
        }
    }
}

void TrackAffectedRef(core::reviews::ReviewProposal& proposal,
                      const parser::AssuranceCase& base_model,
                      const core::reviews::ElementRef& ref) {
    if (ref.existing_id.has_value())
        TrackAffectedExistingElement(proposal, base_model, ref.existing_id.value());
}

void AddHighlightRef(std::unordered_set<std::string>& ids,
                     const core::reviews::ElementRef& ref,
                     const std::map<std::string, std::string>& generated_ids) {
    if (ref.existing_id.has_value() && !ref.existing_id->empty()) {
        ids.insert(ref.existing_id.value());
    }
    if (ref.create_ref.has_value()) {
        auto found = generated_ids.find(ref.create_ref.value());
        if (found != generated_ids.end() && !found->second.empty())
            ids.insert(found->second);
    }
}

bool IsPreviewRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

bool RelationshipTouchesAny(const parser::SacmElement& relationship, const std::unordered_set<std::string>& ids) {
    if (!IsPreviewRelationshipType(relationship.type))
        return false;
    if (ids.count(relationship.reasoning_ref) > 0)
        return true;
    for (const std::string& source : relationship.source_refs) {
        if (ids.count(source) > 0)
            return true;
    }
    for (const std::string& target : relationship.target_refs) {
        if (ids.count(target) > 0)
            return true;
    }
    return false;
}

bool SameRelationship(const parser::SacmElement& lhs, const parser::SacmElement& rhs) {
    return lhs.type == rhs.type && lhs.reasoning_ref == rhs.reasoning_ref && lhs.source_refs == rhs.source_refs &&
           lhs.target_refs == rhs.target_refs;
}

bool RelationshipExists(const parser::AssuranceCase& model, const parser::SacmElement& relationship) {
    for (const parser::SacmElement& element : model.elements) {
        if (!IsPreviewRelationshipType(element.type))
            continue;
        if (!relationship.id.empty() && element.id == relationship.id)
            return true;
        if (SameRelationship(element, relationship))
            return true;
    }
    return false;
}

std::optional<core::RemoveMode> ProposalRemoveModeFromField(const std::string& field) {
    if (field == core::reviews::kReviewProposalRemoveModeNodeOnly)
        return core::RemoveMode::NodeOnly;
    if (field == core::reviews::kReviewProposalRemoveModeNodeAndDescendants)
        return core::RemoveMode::NodeAndDescendants;
    return std::nullopt;
}

std::unordered_set<std::string>
CollectProposalRemovedExistingIds(const core::reviews::ReviewProposal& proposal,
                                  const parser::AssuranceCase& base_model,
                                  const std::map<std::string, std::string>& generated_ids) {
    std::unordered_set<std::string> ids;
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.type != core::reviews::PatchOperationType::RemoveElement || !operation.element.has_value())
            continue;
        const std::string element_id = PreviewIdForProposalRef(operation.element.value(), generated_ids);
        if (element_id.empty() || !parser::FindElementByIdOrGidValue(base_model, element_id))
            continue;

        std::optional<core::RemoveMode> mode = ProposalRemoveModeFromField(operation.field);
        if (mode.has_value()) {
            std::unordered_set<std::string> planned = core::PlanRemoval(base_model, element_id, mode.value());
            ids.insert(planned.begin(), planned.end());
        } else {
            ids.insert(element_id);
        }
    }
    return ids;
}

void RestoreRemovedExistingElementsForProposalPreview(parser::AssuranceCase& preview_model,
                                                      const parser::AssuranceCase& base_model,
                                                      const std::unordered_set<std::string>& removed_ids) {
    if (removed_ids.empty())
        return;

    for (const parser::SacmElement& element : base_model.elements) {
        if (IsPreviewRelationshipType(element.type))
            continue;
        if (removed_ids.count(element.id) == 0)
            continue;
        if (parser::FindElementByIdOrGidValue(preview_model, element.id))
            continue;
        preview_model.elements.push_back(element);
    }

    for (const parser::SacmElement& relationship : base_model.elements) {
        if (!RelationshipTouchesAny(relationship, removed_ids))
            continue;
        if (RelationshipExists(preview_model, relationship))
            continue;
        preview_model.elements.push_back(relationship);
    }
}

std::unordered_set<std::string> CollectProposalHighlightIds(const core::reviews::ReviewProposal& proposal,
                                                            const std::map<std::string, std::string>& generated_ids) {
    std::unordered_set<std::string> ids;
    if (!proposal.anchor_element_id.empty())
        ids.insert(proposal.anchor_element_id);
    for (const std::string& id : proposal.affected_existing_element_ids) {
        if (!id.empty())
            ids.insert(id);
    }
    for (const auto& generated : generated_ids) {
        if (!generated.second.empty())
            ids.insert(generated.second);
    }
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.element.has_value())
            AddHighlightRef(ids, operation.element.value(), generated_ids);
        if (operation.source.has_value())
            AddHighlightRef(ids, operation.source.value(), generated_ids);
        if (operation.target.has_value())
            AddHighlightRef(ids, operation.target.value(), generated_ids);
    }
    return ids;
}

bool DeleteProposalPatchFile(AppRuntimeState& state, const std::string& proposal_id, std::string& error) {
    if (!state.app_state.current_project.has_value()) {
        error = "Open a project before deleting proposed changes.";
        return false;
    }

    core::AssuranceProject& project = state.app_state.current_project.value();
    const std::filesystem::path relative_path = ReviewProposalRelativePath(proposal_id);
    if (ProjectTracksFile(project, relative_path)) {
        return core::ProjectService::RemoveTrackedFile(project, relative_path, true, error);
    }
    return state.proposal_controller->manager.DeleteProposal(proposal_id, error);
}

bool SaveProject(AppRuntimeState& state) {
    if (!state.app_state.current_project.has_value()) {
        state.app_state.status_message = "Create or open a project first.";
        return false;
    }

    if (state.review_controller->IsDirty()) {
        core::AssuranceProject& project = state.app_state.current_project.value();
        std::string error;
        if (!state.review_controller->SaveIfDirty(project, error)) {
            state.app_state.status_message = "Review item save failed: " + error;
            return false;
        }
    }

    if (state.document_dirty) {
        if (!state.app_state.save_project())
            return false;
        state.document_dirty = false;
        state.app_state.has_unsaved_changes = state.review_controller->IsDirty();
        return true;
    }

    if (state.app_state.has_unsaved_changes) {
        state.app_state.has_unsaved_changes = false;
        state.app_state.status_message = "Project saved: " + state.app_state.current_project->name;
        return true;
    }

    return state.app_state.save_project();
}

void ApplyProposalPreviewVisualState(ui::UiState& ui_state,
                                     parser::AssuranceCase& preview_model,
                                     const parser::AssuranceCase& base_model,
                                     const core::reviews::ReviewProposal& proposal,
                                     const std::map<std::string, std::string>& generated_ids) {
    std::unordered_set<std::string> removed_ids =
        CollectProposalRemovedExistingIds(proposal, base_model, generated_ids);
    RestoreRemovedExistingElementsForProposalPreview(preview_model, base_model, removed_ids);

    ui_state.proposal_highlight_ids = CollectProposalHighlightIds(proposal, generated_ids);
    ui_state.proposal_highlight_ids.insert(removed_ids.begin(), removed_ids.end());
    ui_state.marked_for_removal = std::move(removed_ids);
    ui_state.dim_non_proposal_nodes = !ui_state.proposal_highlight_ids.empty();
}

} // namespace

ProposalActions::ProposalActions(AppRuntimeState& state) : state_(state) {}

bool ProposalActions::RefreshCreatorPreview() {
    auto& proposals = *state_.proposal_controller;
    if (!proposals.creator_active)
        return false;
    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "Load a SACM model before editing proposal drafts.");
        return false;
    }

    core::reviews::ReviewProposalPatchService patch_service;
    core::reviews::ProposalPreviewResult preview =
        patch_service.BuildPreviewModel(proposals.draft, state_.app_state.loaded_case.value());
    if (!preview.success) {
        SetStatus(state_, "Proposal draft preview failed: " + preview.error);
        return false;
    }

    proposals.preview_active = false;
    proposals.preview_id = proposals.draft.id;
    proposals.preview_model = std::move(preview.preview_model);
    proposals.creator_generated_ids = std::move(preview.generated_ids);
    ui::UiState& ui_state = ui::GetUiState();
    ApplyProposalPreviewVisualState(ui_state,
                                    proposals.preview_model,
                                    state_.app_state.loaded_case.value(),
                                    proposals.draft,
                                    proposals.creator_generated_ids);
    state_.current_tree = ui::gsn::BuildAssuranceTree(proposals.preview_model);
    ui::gsn::SetCanvasTree(state_.current_tree);
    return true;
}

void ProposalActions::ProcessPendingCreatorPreviewRefresh() {
    auto& proposals = *state_.proposal_controller;
    if (!proposals.creator_preview_refresh_pending)
        return;

    proposals.creator_preview_refresh_pending = false;
    const std::optional<std::string> select_create_ref = proposals.creator_pending_select_create_ref;
    const bool clear_selection = proposals.creator_pending_clear_selection;
    proposals.creator_pending_select_create_ref.reset();
    proposals.creator_pending_clear_selection = false;

    if (!RefreshCreatorPreview())
        return;

    ui::UiState& ui_state = ui::GetUiState();
    if (select_create_ref.has_value()) {
        ui_state.selected_element_id =
            PreviewIdForProposalRef(CreatedElementRef(select_create_ref.value()), proposals.creator_generated_ids);
        ui_state.center_on_selection = !ui_state.selected_element_id.empty();
    } else if (clear_selection) {
        ui_state.selected_element_id.clear();
    }
}

bool ProposalActions::BeginForReviewItem(const core::reviews::ReviewItem& item) {
    auto& proposals = *state_.proposal_controller;
    if (proposals.creator_active) {
        SetStatus(state_, "Save or discard the active proposal before creating another one.");
        return false;
    }
    if (item.status != core::reviews::ReviewItemStatus::Open) {
        SetStatus(state_, "Resolved review comments cannot create proposed changes.");
        return false;
    }
    if (item.proposal_id.has_value()) {
        SetStatus(state_, "This review comment already has a proposed change.");
        return false;
    }
    if (!state_.app_state.current_project.has_value() || !state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "Open a project and SACM file before creating proposed changes.");
        return false;
    }

    const parser::SacmElement* anchor =
        parser::FindElementByIdOrGidValue(state_.app_state.loaded_case.value(), item.element_id);
    if (!anchor) {
        SetStatus(state_, "The reviewed element no longer exists in the loaded model.");
        return false;
    }

    proposals.BeginDraft(item, state_.app_state.loaded_case.value(), *anchor, state_.reviewer_name);
    if (!RefreshCreatorPreview()) {
        CancelActive();
        return false;
    }

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id = anchor->id;
    ui_state.center_on_selection = true;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Building proposal " + proposals.draft.id + ". Use the GSN canvas and Save Proposal when ready.");
    return true;
}

bool ProposalActions::BeginEditForReviewItem(const core::reviews::ReviewItem& item) {
    auto& proposals = *state_.proposal_controller;
    if (proposals.creator_active) {
        SetStatus(state_, "Save or discard the active proposal before editing another one.");
        return false;
    }
    if (item.status != core::reviews::ReviewItemStatus::Open) {
        SetStatus(state_, "Resolved review comments cannot edit proposed changes.");
        return false;
    }
    if (!item.proposal_id.has_value()) {
        SetStatus(state_, "This review comment has no proposed change to edit.");
        return false;
    }
    if (!state_.app_state.current_project.has_value() || !state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "Open a project and SACM file before editing proposed changes.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal =
        proposals.manager.LoadProposal(item.proposal_id.value(), error);
    if (!proposal.has_value()) {
        SetStatus(state_, "Proposal edit failed: " + error);
        return false;
    }
    if (proposal->review_item_id != item.id) {
        SetStatus(state_, "Proposal edit failed: the proposal belongs to a different review comment.");
        return false;
    }

    proposals.BeginEditDraft(std::move(proposal.value()), state_.reviewer_name);
    if (!RefreshCreatorPreview()) {
        CancelActive();
        return false;
    }

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id = proposals.draft.anchor_element_id;
    ui_state.center_on_selection = !ui_state.selected_element_id.empty();
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Editing proposal " + proposals.draft.id + ". Use Save Proposal to update it.");
    return true;
}

bool ProposalActions::BeginEditById(const std::string& proposal_id) {
    if (proposal_id.empty()) {
        SetStatus(state_, "No proposal id was provided.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal =
        state_.proposal_controller->manager.LoadProposal(proposal_id, error);
    if (!proposal.has_value()) {
        SetStatus(state_, "Proposal edit failed: " + error);
        return false;
    }

    std::optional<core::reviews::ReviewItem> item = state_.review_controller->GetItemById(proposal->review_item_id);
    if (!item.has_value()) {
        SetStatus(state_, "Proposal edit failed: the owning review comment was not found.");
        return false;
    }
    if (!item->proposal_id.has_value() || item->proposal_id.value() != proposal_id) {
        SetStatus(state_, "Proposal edit failed: the owning review comment no longer points to this proposal.");
        return false;
    }

    return BeginEditForReviewItem(item.value());
}

bool ProposalActions::PreviewById(const std::string& proposal_id) {
    auto& proposals = *state_.proposal_controller;
    if (proposals.creator_active) {
        SetStatus(state_, "Save or discard the active proposal before viewing another proposal.");
        return false;
    }
    if (proposal_id.empty()) {
        SetStatus(state_, "No proposal id was provided.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal = proposals.manager.LoadProposal(proposal_id, error);
    if (!proposal.has_value()) {
        SetStatus(state_, "Proposal preview failed: " + error);
        return false;
    }

    if (!state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "Load a SACM model before previewing proposals.");
        return false;
    }

    core::reviews::ReviewProposalPatchService patch_service;
    core::reviews::ProposalPreviewResult preview =
        patch_service.BuildPreviewModel(*proposal, state_.app_state.loaded_case.value());
    if (!preview.success) {
        SetStatus(state_, "Proposal preview failed: " + preview.error);
        return false;
    }

    const std::map<std::string, std::string> generated_ids = preview.generated_ids;
    proposals.preview_active = true;
    proposals.preview_id = proposal->id;
    proposals.preview_model = std::move(preview.preview_model);

    ui::UiState& preview_ui_state = ui::GetUiState();
    ApplyProposalPreviewVisualState(
        preview_ui_state, proposals.preview_model, state_.app_state.loaded_case.value(), *proposal, generated_ids);
    state_.current_tree = ui::gsn::BuildAssuranceTree(proposals.preview_model);
    ui::gsn::SetCanvasTree(state_.current_tree);
    preview_ui_state.center_view = ui::CenterView::GsnCanvas;
    preview_ui_state.selected_element_id = proposal->anchor_element_id;
    preview_ui_state.center_on_selection = true;
    state_.workbench.show_gsn_tab = true;
    state_.workbench.force_center_tab_selection = true;

    std::ostringstream status;
    status << "Previewing proposal " << proposal->id << " with " << proposal->operations.size() << " operation(s). ";
    status << "The project model has not been changed.";
    SetStatus(state_, status.str());
    return true;
}

bool ProposalActions::SaveActive(const core::reviews::ReviewItem& item) {
    auto& proposals = *state_.proposal_controller;
    if (!proposals.HasActiveDraftForItem(item.id)) {
        SetStatus(state_, "No active proposal draft for this review comment.");
        return false;
    }
    if (!proposals.CanSaveActiveDraft()) {
        SetStatus(state_, "Add at least one proposal operation before saving.");
        return false;
    }
    if (!state_.app_state.current_project.has_value()) {
        SetStatus(state_, "Open a project before saving proposals.");
        return false;
    }

    core::AssuranceProject& project = state_.app_state.current_project.value();
    core::ProjectFileEntry entry;
    std::string error;
    if (!core::ProjectService::SaveReviewProposalFile(
            project, proposals.draft.id, core::reviews::SerializeReviewProposal(proposals.draft), entry, error)) {
        SetStatus(state_, "Proposal save failed: " + error);
        return false;
    }

    if (!state_.review_controller->SetProposal(item.id, proposals.draft.id)) {
        std::string cleanup_error;
        core::ProjectService::RemoveTrackedFile(project, entry.relativePath, true, cleanup_error);
        SetStatus(state_, "Proposal link update failed.");
        return false;
    }

    const std::string saved_id = proposals.draft.id;
    CancelActive();
    core::ProjectService::RefreshFileStatus(project);
    SetStatus(state_, "Saved proposal " + saved_id + ".");
    return true;
}

bool ProposalActions::ApplyReviewProposal(const core::reviews::ReviewItem& item) {
    if (state_.proposal_controller->creator_active) {
        SetStatus(state_, "Save or discard the active proposal before applying another proposal.");
        return false;
    }
    if (!item.proposal_id.has_value()) {
        SetStatus(state_, "This review comment has no proposed change to apply.");
        return false;
    }
    if (!state_.app_state.current_project.has_value() || !state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "Open a project and SACM file before applying proposed changes.");
        return false;
    }

    std::string error;
    std::optional<core::reviews::ReviewProposal> proposal =
        state_.proposal_controller->manager.LoadProposal(item.proposal_id.value(), error);
    if (!proposal.has_value()) {
        SetStatus(state_, "Proposal apply failed: " + error);
        return false;
    }

    core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(*proposal, state_.app_state.loaded_case.value());
    if (validity.validity != core::reviews::ProposalValidity::Valid) {
        SetStatus(state_, "Proposal is broken: " + validity.reason);
        return false;
    }

    core::reviews::ReviewProposalPatchService patch_service;
    core::reviews::ApplyProposalResult apply_result =
        patch_service.ApplyProposal(*proposal, state_.app_state.loaded_case.value());
    if (!apply_result.success) {
        SetStatus(state_, "Proposal apply failed: " + apply_result.error);
        return false;
    }

    state_.proposal_controller->ClosePreviewIfOpen(item.proposal_id.value());
    ClearProposalHighlightState(ui::GetUiState());
    if (!state_.app_state.sacm_package.has_value())
        state_.app_state.sacm_package.emplace();
    RebuildSacmArgumentPackageFromParser(state_.app_state.loaded_case.value(), state_.app_state.sacm_package.value());
    state_.document_dirty = true;
    state_.app_state.mark_dirty();

    core::AssuranceProject& project = state_.app_state.current_project.value();
    if (!DeleteProposalPatchFile(state_, item.proposal_id.value(), error)) {
        SetStatus(state_, "Proposal applied in memory, but proposal file removal failed: " + error);
        return false;
    }

    const std::string applied_utc = NowUtcString();
    core::reviews::ReviewItem updated = item;
    updated.proposal_id.reset();
    updated.status = core::reviews::ReviewItemStatus::Resolved;
    updated.applied_note = "Proposal applied at " + applied_utc + ".";
    updated.updated_utc = applied_utc;
    if (!state_.review_controller->AddOrUpdateItem(std::move(updated))) {
        SetStatus(state_, "Proposal applied, but review item update failed.");
        return false;
    }

    if (!SaveProject(state_)) {
        SetStatus(state_, "Proposal applied, but project save failed: " + state_.app_state.status_message);
        return false;
    }

    core::ProjectService::RefreshFileStatus(project);
    state_.tree_needs_rebuild = true;
    SetStatus(state_, "Applied proposal " + proposal->id + ".");
    return true;
}

void ProposalActions::CreateAiGenerated(const std::vector<AiReviewProposalSuggestion>& suggestions) {
    if (suggestions.empty())
        return;
    if (!state_.app_state.current_project.has_value() || !state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "AI found proposed text, but a project and SACM file must be open to save proposals.");
        return;
    }

    core::AssuranceProject& project = state_.app_state.current_project.value();
    const parser::AssuranceCase& model = state_.app_state.loaded_case.value();
    size_t saved_count = 0;
    for (const AiReviewProposalSuggestion& suggestion : suggestions) {
        const std::string suggested_text = TrimWhitespace(suggestion.suggested_text);
        if (suggested_text.empty())
            continue;

        std::optional<core::reviews::ReviewItem> item =
            state_.review_controller->GetItemById(suggestion.review_item_id);
        if (!item.has_value() || item->proposal_id.has_value())
            continue;

        const parser::SacmElement* anchor = parser::FindElementByIdOrGidValue(model, item->element_id);
        if (!anchor)
            continue;

        const ElementTextTarget text_target = TextTargetFor(*anchor);
        if (TrimWhitespace(text_target.current_text) == suggested_text)
            continue;

        core::reviews::ReviewProposal proposal = BuildDraftReviewProposal(*item, model, *anchor);
        proposal.author_name = "AI Review";
        proposal.summary = "AI suggested replacement text for " + anchor->id + ".";

        core::reviews::PatchOperation operation;
        operation.type = core::reviews::PatchOperationType::UpdateElementText;
        operation.element = core::reviews::ElementRef{anchor->id, std::nullopt};
        operation.field = text_target.field;
        operation.old_value = text_target.current_text;
        operation.new_value = suggested_text;
        proposal.operations.push_back(std::move(operation));

        core::ProjectFileEntry entry;
        std::string error;
        if (!core::ProjectService::SaveReviewProposalFile(
                project, proposal.id, core::reviews::SerializeReviewProposal(proposal), entry, error)) {
            SetStatus(state_, "AI proposal save failed: " + error);
            continue;
        }

        if (!state_.review_controller->SetProposal(item->id, proposal.id)) {
            std::string cleanup_error;
            core::ProjectService::RemoveTrackedFile(project, entry.relativePath, true, cleanup_error);
            SetStatus(state_, "AI proposal link update failed.");
            continue;
        }
        ++saved_count;
    }

    if (saved_count > 0) {
        core::ProjectService::RefreshFileStatus(project);
        SetStatus(state_,
                  "AI generated " + std::to_string(saved_count) + " proposed change(s). Review before applying.");
    }
}

void ProposalActions::CancelActive() {
    state_.proposal_controller->ClearActiveState();
    ClearProposalHighlightState(ui::GetUiState());

    if (state_.app_state.loaded_case.has_value()) {
        state_.current_tree = ui::gsn::BuildAssuranceTree(state_.app_state.loaded_case.value());
        ui::gsn::SetCanvasTree(state_.current_tree);
    } else {
        state_.tree_needs_rebuild = true;
    }
}

bool ProposalActions::AddChildToSelected(core::NewElementKind kind) {
    auto& proposals = *state_.proposal_controller;
    if (!proposals.creator_active || !state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "Start a proposal draft before editing proposal changes.");
        return false;
    }

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus(state_, "Select an element before adding proposal nodes.");
        return false;
    }

    const parser::SacmElement* parent = parser::FindElementByIdOrGidValue(proposals.preview_model, selected_id);
    if (!parent) {
        SetStatus(state_, "The selected proposal preview element no longer exists.");
        return false;
    }
    const bool parent_is_container = parent->type == "claim" || parent->type == "argumentreasoning";
    if (!parent_is_container) {
        SetStatus(state_, "Cannot add a child to a leaf element (" + parent->type + ").");
        return false;
    }
    if (kind == core::NewElementKind::Strategy && parent->type != "claim") {
        SetStatus(state_, "Strategy can only be added under a Claim.");
        return false;
    }

    std::optional<core::reviews::ElementRef> parent_ref =
        ProposalRefForPreviewId(selected_id, proposals.creator_generated_ids);
    if (!parent_ref.has_value()) {
        SetStatus(state_, "Could not resolve selected element for proposal operation.");
        return false;
    }

    const std::string create_ref = GenerateCreateRef(proposals.draft, kind);

    core::reviews::PatchOperation create;
    create.type = CreateOperationFor(kind);
    create.create_ref = create_ref;
    proposals.draft.operations.push_back(std::move(create));

    core::reviews::PatchOperation relationship;
    relationship.type = IsContextLike(kind) ? core::reviews::PatchOperationType::AddInContextOf
                                            : core::reviews::PatchOperationType::AddSupportedBy;
    relationship.source = CreatedElementRef(create_ref);
    relationship.target = parent_ref.value();
    proposals.draft.operations.push_back(std::move(relationship));

    TrackAffectedRef(proposals.draft, state_.app_state.loaded_case.value(), parent_ref.value());

    proposals.creator_preview_refresh_pending = true;
    proposals.creator_pending_select_create_ref = create_ref;
    proposals.creator_pending_clear_selection = false;
    SetStatus(state_, "Recorded proposal add operation.");
    return true;
}

bool ProposalActions::AddTopGoal() {
    auto& proposals = *state_.proposal_controller;
    if (!proposals.creator_active || !state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "Start a proposal draft before editing proposal changes.");
        return false;
    }

    const std::string create_ref = GenerateCreateRef(proposals.draft, core::NewElementKind::Goal);

    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateClaim;
    create.create_ref = create_ref;
    proposals.draft.operations.push_back(std::move(create));

    proposals.creator_preview_refresh_pending = true;
    proposals.creator_pending_select_create_ref = create_ref;
    proposals.creator_pending_clear_selection = false;
    SetStatus(state_, "Recorded proposal top goal operation.");
    return true;
}

void ProposalActions::RemoveSelected(core::RemoveMode mode) {
    auto& proposals = *state_.proposal_controller;
    if (!proposals.creator_active || !state_.app_state.loaded_case.has_value()) {
        SetStatus(state_, "Start a proposal draft before editing proposal changes.");
        return;
    }

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus(state_, "Select an element before removing proposal nodes.");
        return;
    }

    std::vector<std::string> planned_ids;
    auto planned = core::PlanRemoval(proposals.preview_model, selected_id, mode);
    planned_ids.assign(planned.begin(), planned.end());
    std::sort(planned_ids.begin(), planned_ids.end());
    if (planned_ids.empty()) {
        SetStatus(state_, "Nothing to remove for this selection.");
        return;
    }

    for (const std::string& id : planned_ids) {
        std::optional<core::reviews::ElementRef> ref = ProposalRefForPreviewId(id, proposals.creator_generated_ids);
        if (!ref.has_value())
            continue;
        TrackAffectedRef(proposals.draft, state_.app_state.loaded_case.value(), ref.value());
    }

    std::optional<core::reviews::ElementRef> selected_ref =
        ProposalRefForPreviewId(selected_id, proposals.creator_generated_ids);
    if (!selected_ref.has_value()) {
        SetStatus(state_, "Could not resolve selected element for proposal removal.");
        return;
    }

    proposals.draft.operations.erase(
        std::remove_if(proposals.draft.operations.begin(),
                       proposals.draft.operations.end(),
                       [&](const core::reviews::PatchOperation& operation) {
                           return operation.type == core::reviews::PatchOperationType::RemoveElement &&
                                  operation.element.has_value() &&
                                  SameElementRef(operation.element.value(), selected_ref.value());
                       }),
        proposals.draft.operations.end());

    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    remove.element = selected_ref.value();
    remove.field = RemoveModeField(mode);
    proposals.draft.operations.push_back(std::move(remove));

    proposals.creator_preview_refresh_pending = true;
    proposals.creator_pending_select_create_ref.reset();
    proposals.creator_pending_clear_selection = true;
    SetStatus(state_, "Recorded proposal remove operation.");
}

} // namespace app::actions
