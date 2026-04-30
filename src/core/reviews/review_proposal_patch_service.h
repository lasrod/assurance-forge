#pragma once

#include "core/reviews/review_proposal.h"

#include <map>
#include <string>

namespace core {

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

    ApplyProposalResult ApplyProposal(const ReviewProposal& proposal,
                                      parser::AssuranceCase& current_model) const;
};

}  // namespace core
