#pragma once

#include "core/reviews/review_proposal.h"

#include <map>
#include <string>

namespace core::reviews {

struct ApplyProposalResult {
    bool success = false;
    std::string error;
    std::map<std::string, std::string> generated_ids;
};

struct ProposalPreviewResult {
    bool success = false;
    std::string error;
    parser::AssuranceCase preview_model;
    std::map<std::string, std::string> generated_ids;
};

class ReviewProposalPatchService {
public:
    ProposalPreviewResult BuildPreviewModel(const ReviewProposal& proposal,
                                            const parser::AssuranceCase& current_model) const;

    ApplyProposalResult ApplyProposal(const ReviewProposal& proposal, parser::AssuranceCase& current_model) const;

    // Replay variant: apply the proposal using a caller-provided map of
    // `create_ref` → element id (typically captured into an audit payload
    // during the original apply and restored here). The patch service
    // skips its own deterministic id generation and uses the supplied
    // identities instead. Fails if any required `create_ref` is missing.
    ApplyProposalResult ApplyProposalWithIds(const ReviewProposal& proposal,
                                             parser::AssuranceCase& current_model,
                                             const std::map<std::string, std::string>& predetermined_ids) const;
};

} // namespace core::reviews
