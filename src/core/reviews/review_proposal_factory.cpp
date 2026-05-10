#include "core/reviews/review_proposal_factory.h"

#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_text_utils.h"
#include "core/time_utils.h"
#include "parser/xml_parser.h"

#include <chrono>
#include <sstream>

namespace core::reviews {

std::string GenerateReviewProposalId() {
    static unsigned long long counter = 0;
    auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "proposal-" << std::hex << ticks << "-" << ++counter;
    return out.str();
}

ReviewProposal BuildDraftReviewProposal(const ReviewItem& item,
                                        const parser::AssuranceCase& model,
                                        const parser::SacmElement& anchor) {
    ReviewProposal proposal;
    proposal.id = GenerateReviewProposalId();
    proposal.review_item_id = item.id;
    proposal.title = item.title.empty() ? "Proposed change" : item.title;
    proposal.summary = item.message.empty() ? "Draft proposed change." : TruncateForProblemMessage(item.message, 180);
    proposal.author_name = "Manual reviewer";
    proposal.created_utc = NowUtcString();
    proposal.anchor_element_id = anchor.id;
    proposal.affected_existing_element_ids = {anchor.id};
    proposal.base_model_hash = ComputeModelSemanticHash(model);
    proposal.base_element_hashes[anchor.id] = ComputeElementSemanticHash(anchor);
    return proposal;
}

} // namespace core::reviews