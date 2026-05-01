#include "app/review_problem_sync.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace app {
namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string BuildProblemMessage(const core::reviews::ReviewItem& item) {
    if (item.title.empty()) return item.message;
    if (item.message.empty()) return item.title;
    return item.title + ": " + item.message;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

void ClearProblemsByIdPrefix(core::ProblemsManager& problems_manager, const std::string& prefix) {
    std::vector<std::string> problem_ids;
    for (const core::ProblemItem& problem : problems_manager.GetProblems()) {
        if (StartsWith(problem.id, prefix)) problem_ids.push_back(problem.id);
    }
    for (const std::string& problem_id : problem_ids) {
        problems_manager.RemoveProblem(problem_id);
    }
}

}  // namespace

std::string ReviewProblemId(const core::reviews::ReviewItem& item) {
    return "review-comment:" + item.id;
}

std::string GuidelineReviewProblemId(const core::reviews::ReviewItem& item, const std::string& guideline_id) {
    return "guideline-review:" + item.id + ":" + guideline_id;
}

core::ProblemSeverity ReviewProblemSeverity(const std::string& review_severity) {
    const std::string lowered = ToLower(review_severity);
    if (lowered == "error") return core::ProblemSeverity::Error;
    if (lowered == "warning") return core::ProblemSeverity::Warning;
    return core::ProblemSeverity::Info;
}

core::ProblemItem MakeProblemFromReviewItem(const core::reviews::ReviewItem& item) {
    core::ProblemItem problem;
    problem.id = ReviewProblemId(item);
    problem.severity = ReviewProblemSeverity(item.severity);
    problem.source = core::ProblemSource::ReviewComment;
    problem.element_id = item.element_id;
    problem.type = "ReviewComment";
    problem.message = BuildProblemMessage(item);
    return problem;
}

core::ProblemItem MakeGuidelineProblemFromReviewItem(const core::reviews::ReviewItem& item,
                                                     const std::string& guideline_id) {
    core::ProblemItem problem;
    problem.id = GuidelineReviewProblemId(item, guideline_id);
    problem.severity = ReviewProblemSeverity(item.severity);
    problem.source = core::ProblemSource::GuidelineReview;
    problem.element_id = item.element_id;
    problem.type = "GuidelineReview";
    problem.message = BuildProblemMessage(item);
    problem.guideline_id = guideline_id;
    return problem;
}

void SyncReviewProblems(core::ProblemsManager& problems_manager,
                        const std::vector<core::reviews::ReviewItem>& review_items) {
    ClearProblemsByIdPrefix(problems_manager, "review-comment:");
    ClearProblemsByIdPrefix(problems_manager, "guideline-review:");

    for (const core::reviews::ReviewItem& item : review_items) {
        if (item.status != core::reviews::ReviewItemStatus::Open) continue;
        problems_manager.AddOrUpdateProblem(MakeProblemFromReviewItem(item));
        if (item.source != core::reviews::ReviewItemSource::Manual) continue;

        std::unordered_set<std::string> seen_guideline_ids;
        for (const std::string& guideline_id : item.guideline_ids) {
            if (guideline_id.empty() || seen_guideline_ids.count(guideline_id) > 0) continue;
            problems_manager.AddOrUpdateProblem(MakeGuidelineProblemFromReviewItem(item, guideline_id));
            seen_guideline_ids.insert(guideline_id);
        }
    }
}

}  // namespace app