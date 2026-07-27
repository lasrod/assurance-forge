#include "core/changesets/change_set.h"

#include "core/reviews/review_proposal_patch_service.h"
#include "parser/model_utils.h"

#include <map>
#include <set>
#include <string>

namespace core::changesets {
namespace {

// Two elements differ if anything a reader would notice differs. Deliberately
// not a hash of the whole struct: the render projection carries fields that are
// derived rather than authored, and reporting a node as modified because a
// derived field moved would put a highlight on the canvas the user cannot
// explain.
bool DiffersMeaningfully(const parser::SacmElement& before, const parser::SacmElement& after) {
    return before.name != after.name || before.content != after.content ||
           before.description != after.description || before.undeveloped != after.undeveloped ||
           before.type != after.type || before.source_refs != after.source_refs ||
           before.target_refs != after.target_refs ||
           before.reasoning_ref != after.reasoning_ref ||
           before.assertion_declaration != after.assertion_declaration ||
           before.is_counter != after.is_counter || before.name_langs != after.name_langs ||
           before.content_langs != after.content_langs ||
           before.description_langs != after.description_langs;
}

std::map<std::string, const parser::SacmElement*> ById(const parser::AssuranceCase& model) {
    std::map<std::string, const parser::SacmElement*> index;
    for (const parser::SacmElement& element : model.elements) {
        index[element.id] = &element;
    }
    return index;
}

} // namespace

const char* ChangeSetStateToString(ChangeSetState state) {
    switch (state) {
    case ChangeSetState::Building:
        return "building";
    case ChangeSetState::Ready:
        return "ready";
    case ChangeSetState::Applied:
        return "applied";
    case ChangeSetState::Discarded:
        return "discarded";
    }
    return "building";
}

const char* ElementChangeToString(ElementChange change) {
    switch (change) {
    case ElementChange::Unchanged:
        return "unchanged";
    case ElementChange::Added:
        return "added";
    case ElementChange::Modified:
        return "modified";
    case ElementChange::Removed:
        return "removed";
    }
    return "unchanged";
}

ChangeSetDiff ComputeChangeSetDiff(const ChangeSet&             change_set,
                                   const parser::AssuranceCase& committed) {
    ChangeSetDiff diff;

    if (change_set.proposal.operations.empty()) {
        // An empty change set is legitimate -- the agent has begun one and not
        // staged anything yet -- and previews as the committed model unchanged.
        diff.success       = true;
        diff.preview_model = committed;
        return diff;
    }

    const reviews::ReviewProposalPatchService service;
    const reviews::ProposalPreviewResult      preview =
        service.BuildPreviewModel(change_set.proposal, committed);
    if (!preview.success) {
        diff.error = preview.error;
        return diff;
    }

    diff.success       = true;
    diff.preview_model = preview.preview_model;
    diff.generated_ids = preview.generated_ids;

    const std::map<std::string, const parser::SacmElement*> before = ById(committed);
    const std::map<std::string, const parser::SacmElement*> after  = ById(diff.preview_model);

    for (const std::pair<const std::string, const parser::SacmElement*>& entry : after) {
        const std::map<std::string, const parser::SacmElement*>::const_iterator was =
            before.find(entry.first);
        if (was == before.end()) {
            diff.status_by_id[entry.first] = ElementChange::Added;
            ++diff.added_count;
            continue;
        }
        if (DiffersMeaningfully(*was->second, *entry.second)) {
            diff.status_by_id[entry.first] = ElementChange::Modified;
            ++diff.modified_count;
        } else {
            diff.status_by_id[entry.first] = ElementChange::Unchanged;
        }
    }

    for (const std::pair<const std::string, const parser::SacmElement*>& entry : before) {
        if (after.find(entry.first) != after.end()) {
            continue;
        }
        diff.status_by_id[entry.first] = ElementChange::Removed;
        // Kept whole, not just by id: the preview model no longer holds it, and
        // a reviewer being asked to approve a deletion should be able to see
        // what is being deleted.
        diff.removed.push_back(*entry.second);
        ++diff.removed_count;
    }

    return diff;
}

} // namespace core::changesets
