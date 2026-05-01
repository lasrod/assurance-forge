#pragma once

#include "core/problems/problem_item.h"
#include "core/problems/problems_manager.h"
#include "core/reviews/review_item.h"

#include <string>
#include <vector>

namespace app {

std::string ReviewProblemId(const core::reviews::ReviewItem& item);
core::ProblemSeverity ReviewProblemSeverity(const std::string& review_severity);
core::ProblemItem MakeProblemFromReviewItem(const core::reviews::ReviewItem& item);
void SyncReviewProblems(core::ProblemsManager& problems_manager,
                        const std::vector<core::reviews::ReviewItem>& review_items);

}  // namespace app