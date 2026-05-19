#pragma once

#include <string>

namespace core {

class ProblemsManager;
struct ProblemItem;

void ClearProblemsByIdPrefix(ProblemsManager& problems_manager, const std::string& prefix);

// Returns true when the problem originates from a review workflow
// (open review comments, guideline reviews, or AI reviews). Used by the UI to
// decide whether clicking a problem-driven badge should also offer a "Go to
// Review" jump.
bool IsReviewDerivedProblem(const ProblemItem& problem);

} // namespace core