#include "app/review_problem_sync.h"

#include <gtest/gtest.h>
#include <optional>
#include <utility>

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

} // namespace

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
    problems.AddProblem(core::ProblemItem{"manual-1",
                                          core::ProblemSeverity::Info,
                                          core::ProblemSource::Manual,
                                          "claim-2",
                                          "Claim",
                                          "Existing manual problem",
                                          {}});
    problems.AddProblem(app::MakeProblemFromReviewItem(MakeReviewItem("stale")));

    std::vector<core::reviews::ReviewItem> items = {MakeReviewItem("fresh")};
    app::SyncReviewProblems(problems, items);

    ASSERT_EQ(problems.GetProblems().size(), 2u);
    EXPECT_TRUE(problems.GetProblemById("manual-1").has_value());
    EXPECT_FALSE(problems.GetProblemById("review-comment:stale").has_value());
    EXPECT_TRUE(problems.GetProblemById("review-comment:fresh").has_value());
}

TEST(ReviewProblemSyncTest, TaggedManualReviewItemsCreateGuidelineProblems) {
    core::ProblemsManager problems;
    core::reviews::ReviewItem item = MakeReviewItem("tagged");
    item.source = core::reviews::ReviewItemSource::Manual;
    item.guideline_ids = {"CL.1", "AR.2", "CL.1"};

    app::SyncReviewProblems(problems, {item});

    ASSERT_EQ(problems.GetProblems().size(), 3u);
    ASSERT_TRUE(problems.GetProblemById("review-comment:tagged").has_value());

    std::optional<core::ProblemItem> cl_problem = problems.GetProblemById("guideline-review:tagged:CL.1");
    ASSERT_TRUE(cl_problem.has_value());
    EXPECT_EQ(cl_problem->source, core::ProblemSource::GuidelineReview);
    EXPECT_EQ(cl_problem->element_id, "claim-1");
    EXPECT_EQ(cl_problem->guideline_id, "CL.1");
    EXPECT_EQ(cl_problem->message, "Missing context: Add rationale for the assumption.");

    std::optional<core::ProblemItem> ar_problem = problems.GetProblemById("guideline-review:tagged:AR.2");
    ASSERT_TRUE(ar_problem.has_value());
    EXPECT_EQ(ar_problem->guideline_id, "AR.2");
}

TEST(ReviewProblemSyncTest, GuidelineProblemsOnlyComeFromOpenManualReviewItems) {
    core::ProblemsManager problems;
    core::reviews::ReviewItem manual_open = MakeReviewItem("manual-open");
    manual_open.source = core::reviews::ReviewItemSource::Manual;
    manual_open.guideline_ids = {"CL.1"};

    core::reviews::ReviewItem manual_resolved = MakeReviewItem("manual-resolved");
    manual_resolved.source = core::reviews::ReviewItemSource::Manual;
    manual_resolved.status = core::reviews::ReviewItemStatus::Resolved;
    manual_resolved.guideline_ids = {"CL.2"};

    core::reviews::ReviewItem ai_open = MakeReviewItem("ai-open");
    ai_open.source = core::reviews::ReviewItemSource::AIReview;
    ai_open.guideline_ids = {"CL.3"};

    app::SyncReviewProblems(problems, {manual_open, manual_resolved, ai_open});

    EXPECT_TRUE(problems.GetProblemById("review-comment:manual-open").has_value());
    EXPECT_TRUE(problems.GetProblemById("review-comment:ai-open").has_value());
    EXPECT_TRUE(problems.GetProblemById("guideline-review:manual-open:CL.1").has_value());
    EXPECT_FALSE(problems.GetProblemById("review-comment:manual-resolved").has_value());
    EXPECT_FALSE(problems.GetProblemById("guideline-review:manual-resolved:CL.2").has_value());
    EXPECT_FALSE(problems.GetProblemById("guideline-review:ai-open:CL.3").has_value());
}

TEST(ReviewProblemSyncTest, SyncReviewProblemsClearsStaleGuidelineProblemsByPrefix) {
    core::ProblemsManager problems;
    core::ProblemItem unrelated_guideline_problem;
    unrelated_guideline_problem.id = "external-guideline-review:keep";
    unrelated_guideline_problem.source = core::ProblemSource::GuidelineReview;
    unrelated_guideline_problem.guideline_id = "CL.9";
    problems.AddProblem(unrelated_guideline_problem);

    core::reviews::ReviewItem stale = MakeReviewItem("stale");
    stale.guideline_ids = {"CL.1"};
    problems.AddProblem(app::MakeGuidelineProblemFromReviewItem(stale, "CL.1"));

    app::SyncReviewProblems(problems, {});

    EXPECT_TRUE(problems.GetProblemById("external-guideline-review:keep").has_value());
    EXPECT_FALSE(problems.GetProblemById("guideline-review:stale:CL.1").has_value());
}