#include "app/proposal_adoption.h"

#include <unordered_set>

namespace app {

std::vector<ProposalAdoption>
PlanProposalAdoption(const std::vector<core::reviews::ReviewProposal>& proposals,
                     const std::vector<core::reviews::ReviewItem>&     existing_items) {
    std::unordered_set<std::string> linked;
    for (const core::reviews::ReviewItem& item : existing_items) {
        if (item.proposal_id.has_value()) {
            linked.insert(item.proposal_id.value());
        }
    }

    std::vector<ProposalAdoption> adoptions;
    for (const core::reviews::ReviewProposal& proposal : proposals) {
        if (proposal.id.empty() || linked.count(proposal.id) > 0) {
            continue;
        }

        ProposalAdoption adoption;
        adoption.proposal_id = proposal.id;

        core::reviews::ReviewItem& item = adoption.item;
        // Prefixed and derived from the proposal id so a second pass over the
        // same file updates the item rather than creating a duplicate.
        item.id         = "external-proposal:" + proposal.id;
        item.element_id = proposal.anchor_element_id;
        item.title = proposal.title.empty() ? std::string("Proposed change") : proposal.title;
        item.message       = proposal.summary;
        item.severity      = "info";
        item.reviewer_name =
            proposal.author_name.empty() ? std::string("External tool") : proposal.author_name;
        item.source      = core::reviews::ReviewItemSource::AIReview;
        item.status      = core::reviews::ReviewItemStatus::Open;
        item.proposal_id = proposal.id;
        item.created_utc = proposal.created_utc;
        item.updated_utc = proposal.created_utc;

        adoptions.push_back(std::move(adoption));
    }
    return adoptions;
}

} // namespace app
