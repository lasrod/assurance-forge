#pragma once

#include <string>

namespace parser {
struct AssuranceCase;
struct SacmElement;
} // namespace parser

namespace core::reviews {

struct ReviewItem;
struct ReviewProposal;

std::string GenerateReviewProposalId();
ReviewProposal
BuildDraftReviewProposal(const ReviewItem& item, const parser::AssuranceCase& model, const parser::SacmElement& anchor);

} // namespace core::reviews