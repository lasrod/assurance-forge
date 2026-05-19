#pragma once

#include "core/problems/problem_item.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core {

bool ShouldHighlightProblemAttention(const ProblemItem& problem);
std::unordered_set<std::string> CollectAttentionElementIds(const std::vector<ProblemItem>& problems);

// Per-element aggregate used to drive UI alert badges. Built from the current
// ProblemsManager snapshot; the badge icon and colour reflect the highest
// severity, and `has_review_problem` enables a one-click "Go to Review" jump
// from the Problems panel.
struct ElementBadgeSummary {
    ProblemSeverity highest_severity = ProblemSeverity::Info;
    std::string top_problem_id;       // highest-severity problem id (stable: tie-broken by id)
    std::string top_problem_message;  // copied from the highest-severity problem for tooltips
    std::size_t problem_count = 0;
    bool has_review_problem = false;
};

// Reduce a problem snapshot to a per-element summary. Skips problems that
// `ShouldHighlightProblemAttention` rejects (e.g. ImportExport, global).
std::unordered_map<std::string, ElementBadgeSummary>
BuildElementBadgeSummaries(const std::vector<ProblemItem>& problems);

// Returns the highest-severity problem affecting `element_id`, or nullptr if
// none. Tie-broken by problem id for stable ordering across frames.
const ProblemItem* HighestSeverityProblemForElement(const std::vector<ProblemItem>& problems,
                                                    const std::string& element_id);

} // namespace core