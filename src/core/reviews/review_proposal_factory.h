#pragma once

#include "core/sacm_model.h"

#include <string>

namespace core::reviews {

struct ReviewItem;
struct ReviewProposal;

std::string GenerateReviewProposalId();
ReviewProposal
BuildDraftReviewProposal(const ReviewItem& item, const parser::AssuranceCase& model, const parser::SacmElement& anchor);

} // namespace core::reviews