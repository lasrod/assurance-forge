#include "core/drafts/draft_promotion_service.h"

#include "core/time_utils.h"

#include <algorithm>

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
    CompiledDraftPromotion result;

    const std::vector<const DraftChangeGroup*> active = workspace.ActiveGroups();
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

} // namespace core::drafts
