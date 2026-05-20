#include "core/problems/problem_attention.h"

#include "core/problems/problem_utils.h"

namespace core {

namespace {

// Numeric ordering for ProblemSeverity so we can compare with `>`. Error is
// the highest, Info the lowest.
int SeverityRank(ProblemSeverity severity) {
    switch (severity) {
    case ProblemSeverity::Error:
        return 2;
    case ProblemSeverity::Warning:
        return 1;
    case ProblemSeverity::Info:
        return 0;
    }
    return 0;
}

// True when `candidate` should replace the currently-selected highest-severity
// problem. Higher severity wins; on ties, lower id wins for stable ordering.
bool ShouldPromoteAsTop(const ProblemItem& current, const ProblemItem& candidate) {
    const int cur_rank = SeverityRank(current.severity);
    const int cand_rank = SeverityRank(candidate.severity);
    if (cand_rank != cur_rank)
        return cand_rank > cur_rank;
    return candidate.id < current.id;
}

} // namespace

bool ShouldHighlightProblemAttention(const ProblemItem& problem) {
    if (problem.element_id.empty())
        return false;

    switch (problem.source) {
    case ProblemSource::ImportExport:
        return false;
    case ProblemSource::Manual:
    case ProblemSource::ReviewComment:
    case ProblemSource::ModelValidation:
    case ProblemSource::GuidelineReview:
    case ProblemSource::AIReview:
        return true;
    }

    return false;
}

std::unordered_set<std::string> CollectAttentionElementIds(const std::vector<ProblemItem>& problems) {
    std::unordered_set<std::string> element_ids;
    for (const ProblemItem& problem : problems) {
        if (!ShouldHighlightProblemAttention(problem))
            continue;
        element_ids.insert(problem.element_id);
    }
    return element_ids;
}

std::unordered_map<std::string, ElementBadgeSummary>
BuildElementBadgeSummaries(const std::vector<ProblemItem>& problems) {
    std::unordered_map<std::string, ElementBadgeSummary> summaries;

    // Track the currently-chosen "top problem" for each element so we can
    // compare new candidates against it without doing a second pass.
    std::unordered_map<std::string, const ProblemItem*> top_problem_ptrs;
    // Separate tracker for the highest-severity *review-derived* problem so
    // "Go to Review" affordances can describe what they will open, even when
    // a higher-severity non-review problem dominates the overall summary.
    std::unordered_map<std::string, const ProblemItem*> top_review_problem_ptrs;

    for (const ProblemItem& problem : problems) {
        if (!ShouldHighlightProblemAttention(problem))
            continue;

        ElementBadgeSummary& summary = summaries[problem.element_id];
        summary.problem_count += 1;

        auto top_it = top_problem_ptrs.find(problem.element_id);
        if (top_it == top_problem_ptrs.end() || ShouldPromoteAsTop(*top_it->second, problem)) {
            top_problem_ptrs[problem.element_id] = &problem;
            summary.highest_severity = problem.severity;
            summary.top_problem_id = problem.id;
            summary.top_problem_message = problem.message;
        }

        if (IsReviewDerivedProblem(problem)) {
            summary.has_review_problem = true;
            auto rv_it = top_review_problem_ptrs.find(problem.element_id);
            if (rv_it == top_review_problem_ptrs.end() || ShouldPromoteAsTop(*rv_it->second, problem)) {
                top_review_problem_ptrs[problem.element_id] = &problem;
                summary.top_review_severity = problem.severity;
                summary.top_review_problem_id = problem.id;
                summary.top_review_problem_message = problem.message;
            }
        }
    }

    return summaries;
}

const ProblemItem* HighestSeverityProblemForElement(const std::vector<ProblemItem>& problems,
                                                    const std::string& element_id) {
    if (element_id.empty())
        return nullptr;
    const ProblemItem* top = nullptr;
    for (const ProblemItem& problem : problems) {
        if (problem.element_id != element_id)
            continue;
        if (!ShouldHighlightProblemAttention(problem))
            continue;
        if (top == nullptr || ShouldPromoteAsTop(*top, problem))
            top = &problem;
    }
    return top;
}

} // namespace core