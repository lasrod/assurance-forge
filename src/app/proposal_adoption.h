#pragma once

// Deciding which proposal files this application should take ownership of.
//
// The MCP server writes proposal files and nothing else. It used to also write
// the review item that surfaces one and the manifest entry that tracks it, which
// made it a second writer of files this application saves whole -- so the next
// save here reverted both and the proposal silently vanished. Adoption restores
// a single writer per file: that process owns `reviews/proposals/`, this one owns
// the manifest and review items and picks up whatever appears there.
//
// The decision is separated from the I/O so it can be tested without standing up
// an application runtime.

#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"

#include <optional>
#include <string>
#include <vector>

namespace app {

// One proposal this application has not yet claimed.
struct ProposalAdoption {
    core::reviews::ReviewItem item;
    std::string               proposal_id;
};

// A proposal created here is always linked to a review item, so an unlinked one
// came from somewhere else. `existing_items` is the current review set;
// `proposals` are the drafts found on disk.
std::vector<ProposalAdoption>
PlanProposalAdoption(const std::vector<core::reviews::ReviewProposal>& proposals,
                     const std::vector<core::reviews::ReviewItem>&     existing_items);

} // namespace app
