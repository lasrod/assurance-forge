#include "app/actions/proposal_actions_internal.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/project_workflow.h"
#include "core/project_service.h"
#include "parser/model_utils.h"
#include "ui/i18n/localization.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace app::actions::detail {

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

namespace {

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

        const auto planned =
            ProposalRemoveModeFromField(operation.field)
                .transform([&](core::RemoveMode m) { return core::PlanRemoval(base_model, element_id, m); })
                .value_or(std::unordered_set<std::string>{element_id});
        ids.insert(planned.begin(), planned.end());
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

std::unordered_map<std::string, std::vector<ui::ProposalTextChangePreview>>
CollectProposalTextChanges(const core::reviews::ReviewProposal& proposal,
                           const std::map<std::string, std::string>& generated_ids) {
    std::unordered_map<std::string, std::vector<ui::ProposalTextChangePreview>> changes_by_element;
    for (const core::reviews::PatchOperation& operation : proposal.operations) {
        if (operation.type != core::reviews::PatchOperationType::UpdateElementText &&
            operation.type != core::reviews::PatchOperationType::UpdateElementName) {
            continue;
        }
        if (!operation.element.has_value() || operation.old_value == operation.new_value)
            continue;

        const std::string element_id = PreviewIdForProposalRef(operation.element.value(), generated_ids);
        if (element_id.empty())
            continue;

        changes_by_element[element_id].push_back(ui::ProposalTextChangePreview{
            operation.field.empty() ? "text" : operation.field,
            operation.old_value,
            operation.new_value,
        });
    }
    return changes_by_element;
}

} // namespace

bool IsContextLike(core::NewElementKind kind) {
    return kind == core::NewElementKind::Context || kind == core::NewElementKind::Assumption ||
           kind == core::NewElementKind::Justification;
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
        state.app_state.status_message = AF_TR("Create or open a project first.");
        return false;
    }

    if (state.review_controller->IsDirty()) {
        core::AssuranceProject& project = state.app_state.current_project.value();
        std::string error;
        if (!state.review_controller->SaveIfDirty(project, error)) {
            state.app_state.status_message = ui::i18n::trf("Review item save failed: {0}", error);
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
        state.app_state.status_message = ui::i18n::trf("Project saved: {0}", state.app_state.current_project->name);
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
    ui_state.proposal_text_changes = CollectProposalTextChanges(proposal, generated_ids);
    ui_state.proposal_highlight_ids.insert(removed_ids.begin(), removed_ids.end());
    ui_state.marked_for_removal = std::move(removed_ids);
    ui_state.dim_non_proposal_nodes = !ui_state.proposal_highlight_ids.empty();
}

} // namespace app::actions::detail
