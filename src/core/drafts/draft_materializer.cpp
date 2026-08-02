#include "core/drafts/draft_materializer.h"

#include "core/reviews/review_proposal_patch_service.h"

#include <algorithm>
#include <unordered_set>

namespace core::drafts {
namespace {

bool IsCreateOperation(reviews::PatchOperationType type) {
    switch (type) {
    case reviews::PatchOperationType::CreateClaim:
    case reviews::PatchOperationType::CreateStrategy:
    case reviews::PatchOperationType::CreateSolution:
    case reviews::PatchOperationType::CreateContext:
    case reviews::PatchOperationType::CreateAssumption:
    case reviews::PatchOperationType::CreateJustification:
        return true;
    default:
        return false;
    }
}

// A group's operations in the vocabulary the patch service speaks. The draft
// domain deliberately reuses `ReviewProposal` rather than inventing an operation
// format, so promotion needs no new command type.
reviews::ReviewProposal ProposalForGroup(const DraftChangeGroup& group) {
    reviews::ReviewProposal proposal;
    proposal.id = group.id;
    proposal.title = group.title;
    proposal.summary = group.summary;
    proposal.author_name = group.source_label;
    proposal.created_utc = group.created_utc;
    proposal.operations = group.operations;
    return proposal;
}

// Every identity pinned anywhere in the workspace.
//
// Needed because a group later in the sequence has already pinned ids that are
// not yet in the model when an earlier group allocates. Without this, extending
// an early group can hand it an id a later group is already using, and the later
// group then fails to apply against an id that already exists.
std::unordered_set<std::string> PinnedIdentities(const DraftWorkspace& workspace) {
    std::unordered_set<std::string> pinned;
    for (const DraftChangeGroup& group : workspace.groups) {
        if (!group.active())
            continue;
        for (const auto& [create_ref, id] : group.generated_ids)
            pinned.insert(id);
    }
    return pinned;
}

bool GroupNeedsIdentities(const DraftChangeGroup& group) {
    for (const reviews::PatchOperation& operation : group.operations) {
        if (!IsCreateOperation(operation.type))
            continue;
        if (!operation.create_ref.has_value())
            return true;
        if (group.generated_ids.count(operation.create_ref.value()) == 0)
            return true;
    }
    return false;
}

// Applies one group, allocating only the identities it does not already have.
//
// Always goes through `ApplyProposalWithIds`, never `ApplyProposal`: the latter
// regenerates every id from scratch, which would rename the elements an earlier
// materialization already told an agent about.
bool ApplyGroup(DraftChangeGroup& group,
                core::AssuranceCase& model,
                const std::unordered_set<std::string>& reserved,
                bool& allocated_identities,
                std::string& error) {
    const reviews::ReviewProposal proposal = ProposalForGroup(group);

    if (GroupNeedsIdentities(group)) {
        std::map<std::string, std::string> identities = group.generated_ids;
        if (!reviews::AllocateProposalIds(proposal, model, reserved, identities, error))
            return false;
        group.generated_ids = std::move(identities);
        allocated_identities = true;
    }

    const reviews::ReviewProposalPatchService service;
    const reviews::ApplyProposalResult result = service.ApplyProposalWithIds(proposal, model, group.generated_ids);
    if (!result.success) {
        error = result.error;
        return false;
    }
    return true;
}

bool TouchesAny(const std::vector<std::string>& candidates, const std::unordered_set<std::string>& changed) {
    return std::any_of(candidates.begin(), candidates.end(), [&](const std::string& candidate) {
        return !candidate.empty() && changed.count(candidate) > 0;
    });
}

// Findings the draft is answerable for.
//
// The whole-model Problems pipeline reports everything; a group row must report
// only what this draft touched, or an agent is handed defects in parts of the
// argument it never wrote and a reviewer cannot tell which are new.
void CollectFindings(DraftMaterializationResult& result) {
    const std::vector<std::string> changed_ids = result.change_index.ChangedElementIds();
    if (changed_ids.empty())
        return;

    const std::unordered_set<std::string> changed(changed_ids.begin(), changed_ids.end());

    result.sccg_findings = core::sccg::CheckStagedArgument(result.working_model, changed_ids);

    for (const core::GsnFinding& finding : core::CheckGsnWellFormedness(result.working_model)) {
        if (TouchesAny({finding.element_id, finding.related_id, finding.relationship_id}, changed))
            result.gsn_findings.push_back(finding);
    }

    for (const core::ArgumentCycle& cycle : core::FindSupportCycles(result.working_model)) {
        if (TouchesAny(cycle.element_ids, changed))
            result.cycles.push_back(cycle);
    }
}

} // namespace

DraftMaterializationResult MaterializeDraft(DraftWorkspace& workspace, const core::AssuranceCase& accepted) {
    DraftMaterializationResult result;
    // On failure the caller gets the accepted argument back, not a model with
    // some groups applied. A partially-applied safety argument shown as if it
    // were the draft is the failure mode this exists to prevent.
    result.working_model = accepted;

    const std::vector<const DraftChangeGroup*> active = workspace.ActiveGroups();
    if (active.empty()) {
        result.success = true;
        return result;
    }

    const std::unordered_set<std::string> reserved = PinnedIdentities(workspace);

    core::AssuranceCase working = accepted;
    DraftChangeIndex index;

    for (const DraftChangeGroup* ordered : active) {
        DraftChangeGroup* group = workspace.FindGroup(ordered->id);
        if (group == nullptr) {
            result.error = "Draft group disappeared during materialization: " + ordered->id;
            result.failing_group_id = ordered->id;
            return result;
        }

        const core::AssuranceCase before = working;
        std::string error;
        if (!ApplyGroup(*group, working, reserved, result.allocated_identities, error)) {
            result.error = error;
            result.failing_group_id = group->id;
            return result;
        }
        RecordGroupContributions(index, group->id, before, working);
    }

    FinalizeChangeIndex(index, accepted, working);

    result.working_model = std::move(working);
    result.change_index = std::move(index);
    CollectFindings(result);
    result.success = true;
    return result;
}

bool CanStageOperations(const DraftWorkspace& workspace,
                        const core::AssuranceCase& accepted,
                        const std::string& group_id,
                        const std::vector<reviews::PatchOperation>& operations,
                        std::string& error) {
    // Rehearsed on a copy, so a refusal costs nothing and an acceptance leaves
    // no identities pinned by a staging attempt that the caller may still
    // reject.
    DraftWorkspace rehearsal = workspace;
    DraftChangeGroup* group = rehearsal.FindGroup(group_id);
    if (group == nullptr) {
        error = "No draft change group with id " + group_id + ".";
        return false;
    }
    group->operations.insert(group->operations.end(), operations.begin(), operations.end());

    const DraftMaterializationResult result = MaterializeDraft(rehearsal, accepted);
    if (!result.success) {
        error = result.error;
        return false;
    }
    return true;
}

} // namespace core::drafts
