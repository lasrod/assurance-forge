#include "app/review_problem_sync.h"

#include <algorithm>
#include <cctype>
#include <string>

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

}  // namespace

std::string ReviewProblemId(const core::reviews::ReviewItem& item) {
    return "review-comment:" + item.id;
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

void SyncReviewProblems(core::ProblemsManager& problems_manager,
                        const std::vector<core::reviews::ReviewItem>& review_items) {
    problems_manager.ClearProblemsBySource(core::ProblemSource::ReviewComment);

    for (const core::reviews::ReviewItem& item : review_items) {
        if (item.status != core::reviews::ReviewItemStatus::Open) continue;
        problems_manager.AddOrUpdateProblem(MakeProblemFromReviewItem(item));
    }
}

}  // namespace app