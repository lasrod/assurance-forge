#include "core/problems/problem_attention.h"

namespace core {

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

} // namespace core