#include <gtest/gtest.h>

#include "app/review_problem_sync.h"

namespace {

core::reviews::ReviewItem MakeReviewItem(std::string id = "review-1") {
    core::reviews::ReviewItem item;
    item.id = std::move(id);
    item.element_id = "claim-1";
    item.title = "Missing context";
    item.message = "Add rationale for the assumption.";
    item.severity = "warning";
    item.status = core::reviews::ReviewItemStatus::Open;
    return item;
}

}  // namespace

TEST(ReviewProblemSyncTest, MakeProblemFromReviewItemUsesStableFields) {
    const core::reviews::ReviewItem item = MakeReviewItem();

    const core::ProblemItem problem = app::MakeProblemFromReviewItem(item);

    EXPECT_EQ(problem.id, "review-comment:review-1");
    EXPECT_EQ(problem.source, core::ProblemSource::ReviewComment);
    EXPECT_EQ(problem.severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(problem.element_id, "claim-1");
    EXPECT_EQ(problem.type, "ReviewComment");
    EXPECT_EQ(problem.message, "Missing context: Add rationale for the assumption.");
}

TEST(ReviewProblemSyncTest, SyncReviewProblemsIncludesOnlyOpenItems) {
    core::ProblemsManager problems;
    std::vector<core::reviews::ReviewItem> items;

    core::reviews::ReviewItem open_item = MakeReviewItem("open-1");
    core::reviews::ReviewItem resolved_item = MakeReviewItem("resolved-1");
    resolved_item.status = core::reviews::ReviewItemStatus::Resolved;
    items.push_back(open_item);
    items.push_back(resolved_item);

    app::SyncReviewProblems(problems, items);

    ASSERT_EQ(problems.GetProblems().size(), 1u);
    ASSERT_TRUE(problems.GetProblemById("review-comment:open-1").has_value());
    EXPECT_FALSE(problems.GetProblemById("review-comment:resolved-1").has_value());
}

TEST(ReviewProblemSyncTest, SyncReviewProblemsClearsStaleReviewProblemsOnly) {
    core::ProblemsManager problems;
    problems.AddProblem(core::ProblemItem{
        "manual-1",
        core::ProblemSeverity::Info,
        core::ProblemSource::Manual,
        "claim-2",
        "Claim",
        "Existing manual problem",
        {}
    });
    problems.AddProblem(app::MakeProblemFromReviewItem(MakeReviewItem("stale")));

    std::vector<core::reviews::ReviewItem> items = { MakeReviewItem("fresh") };
    app::SyncReviewProblems(problems, items);

    ASSERT_EQ(problems.GetProblems().size(), 2u);
    EXPECT_TRUE(problems.GetProblemById("manual-1").has_value());
    EXPECT_FALSE(problems.GetProblemById("review-comment:stale").has_value());
    EXPECT_TRUE(problems.GetProblemById("review-comment:fresh").has_value());
}