#include "core/drafts/draft_promotion_service.h"

#include "core/drafts/draft_dependency_graph.h"
#include "core/drafts/draft_materializer.h"
#include "core/time_utils.h"

#include <algorithm>
#include <unordered_set>

namespace core::drafts {
namespace {

// `$goal` in group-2 becomes `$group-2::goal`.
//
// Two clients working on one argument have no way to coordinate the patch-local
// names they choose, and `$goal` is the obvious choice for both. Concatenating
// without this would make one element out of two proposed claims -- and the loss
// would be silent, because the result still applies.
std::string NamespaceRef(const std::string& group_id, const std::string& create_ref) {
    if (create_ref.empty() || create_ref.front() != '$')
        return create_ref;
    return "$" + group_id + "::" + create_ref.substr(1);
}

void NamespaceElementRef(const std::string& group_id, std::optional<reviews::ElementRef>& ref) {
    if (!ref.has_value() || !ref->create_ref.has_value())
        return;
    ref->create_ref = NamespaceRef(group_id, ref->create_ref.value());
}

} // namespace

CompiledDraftPromotion CompileWorkspacePromotion(const DraftWorkspace& workspace, const std::string& author_name) {
    std::vector<std::string> every_active;
    for (const DraftChangeGroup* group : workspace.ActiveGroups())
        every_active.push_back(group->id);
    return CompileSelectedPromotion(workspace, every_active, author_name);
}

CompiledDraftPromotion CompileSelectedPromotion(const DraftWorkspace& workspace,
                                                const std::vector<std::string>& group_ids,
                                                const std::string& author_name) {
    CompiledDraftPromotion result;

    const std::unordered_set<std::string> selected(group_ids.begin(), group_ids.end());
    std::vector<const DraftChangeGroup*> active;
    for (const DraftChangeGroup* group : workspace.ActiveGroups()) {
        if (selected.count(group->id) > 0)
            active.push_back(group);
    }
    if (active.empty()) {
        result.error = "There is nothing in this draft to accept.";
        return result;
    }

    result.proposal.schema = reviews::kReviewProposalSchema;
    result.proposal.id = workspace.id;
    result.proposal.title = "Working draft";
    result.proposal.author_name = author_name;
    result.proposal.created_utc = NowUtcString();

    for (const DraftChangeGroup* group : active) {
        if (group->operations.empty())
            continue;

        for (const reviews::PatchOperation& source : group->operations) {
            reviews::PatchOperation operation = source;
            if (operation.create_ref.has_value())
                operation.create_ref = NamespaceRef(group->id, operation.create_ref.value());
            NamespaceElementRef(group->id, operation.element);
            NamespaceElementRef(group->id, operation.source);
            NamespaceElementRef(group->id, operation.target);
            result.proposal.operations.push_back(std::move(operation));
        }

        for (const auto& [create_ref, element_id] : group->generated_ids)
            result.identities[NamespaceRef(group->id, create_ref)] = element_id;

        result.group_ids.push_back(group->id);
        const std::string label =
            group->source_label.empty() ? std::string(DraftSourceToString(group->source)) : group->source_label;
        if (std::find(result.source_labels.begin(), result.source_labels.end(), label) == result.source_labels.end())
            result.source_labels.push_back(label);

        if (!group->summary.empty()) {
            if (!result.proposal.summary.empty())
                result.proposal.summary += "\n";
            result.proposal.summary += group->title.empty() ? group->summary : group->title + ": " + group->summary;
        }
    }

    if (result.proposal.operations.empty()) {
        result.error = "There is nothing in this draft to accept.";
        return result;
    }

    result.success = true;
    return result;
}

DraftPromotionPlan PlanDraftPromotion(const DraftWorkspace& workspace,
                                      const core::AssuranceCase& accepted,
                                      const std::vector<std::string>& selection,
                                      const std::string& author_name) {
    DraftPromotionPlan plan;
    if (selection.empty()) {
        plan.error = "Nothing was selected to accept.";
        return plan;
    }

    // Never the raw selection. Accepting a claim without the strategy and the
    // relationships that carry it is not a smaller change -- it is an argument
    // that asserts support it has not got.
    plan.closure = DependencyClosure(workspace, selection);
    if (plan.closure.empty()) {
        plan.error = "Nothing was selected to accept.";
        return plan;
    }
    const std::unordered_set<std::string> asked_for(selection.begin(), selection.end());
    for (const std::string& group_id : plan.closure) {
        if (asked_for.count(group_id) == 0)
            plan.added_by_closure.push_back(group_id);
    }

    const std::unordered_set<std::string> promoting(plan.closure.begin(), plan.closure.end());

    // What the accepted argument becomes. Rehearsed on a copy of the workspace so
    // no identity is pinned by a promotion that may still be refused.
    DraftWorkspace selected_only = workspace;
    for (DraftChangeGroup& group : selected_only.groups) {
        if (group.active() && promoting.count(group.id) == 0)
            group.state = DraftGroupState::Rejected;
    }
    const DraftMaterializationResult promoted = MaterializeDraft(selected_only, accepted);
    if (!promoted.success) {
        plan.error = promoted.error;
        return plan;
    }

    // And what is left of the draft afterwards, against that prospective
    // baseline. A selection that applies cleanly on its own can still leave the
    // rest of the draft referring to something it removed, and finding that out
    // after the accepted SACM had already changed would mean a safety argument
    // in a state nobody chose.
    DraftWorkspace remainder = workspace;
    bool has_remainder = false;
    for (DraftChangeGroup& group : remainder.groups) {
        if (!group.active())
            continue;
        if (promoting.count(group.id) > 0) {
            group.state = DraftGroupState::Rejected;
        } else {
            has_remainder = true;
        }
    }
    if (has_remainder) {
        const DraftMaterializationResult rebased = MaterializeDraft(remainder, promoted.working_model);
        if (!rebased.success) {
            plan.error = "The rest of the draft cannot be rebased onto this result: " + rebased.error;
            return plan;
        }
    }

    plan.compiled = CompileSelectedPromotion(workspace, plan.closure, author_name);
    if (!plan.compiled.success) {
        plan.error = plan.compiled.error;
        return plan;
    }

    plan.promoted_model = promoted.working_model;
    plan.ok = true;
    return plan;
}

} // namespace core::drafts
