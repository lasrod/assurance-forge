#include "core/problems/problem_attention.h"

#include <gtest/gtest.h>

namespace {

core::ProblemItem MakeProblem(const std::string& id, core::ProblemSource source, const std::string& element_id) {
    core::ProblemItem problem;
    problem.id = id;
    problem.source = source;
    problem.element_id = element_id;
    problem.type = "Claim";
    problem.message = "Problem";
    return problem;
}

} // namespace

TEST(ProblemAttentionTest, HighlightsElementScopedReviewAndValidationProblems) {
    std::vector<core::ProblemItem> problems = {
        MakeProblem("review", core::ProblemSource::ReviewComment, "G-1"),
        MakeProblem("validation", core::ProblemSource::ModelValidation, "G-2"),
        MakeProblem("guideline", core::ProblemSource::GuidelineReview, "G-3"),
        MakeProblem("ai", core::ProblemSource::AIReview, "G-4"),
        MakeProblem("manual", core::ProblemSource::Manual, "G-5"),
    };

    const std::unordered_set<std::string> attention = core::CollectAttentionElementIds(problems);

    EXPECT_EQ(attention.size(), 5u);
    EXPECT_TRUE(attention.count("G-1") > 0);
    EXPECT_TRUE(attention.count("G-2") > 0);
    EXPECT_TRUE(attention.count("G-3") > 0);
    EXPECT_TRUE(attention.count("G-4") > 0);
    EXPECT_TRUE(attention.count("G-5") > 0);
}

TEST(ProblemAttentionTest, IgnoresProblemsWithoutElementIdsAndImportExport) {
    std::vector<core::ProblemItem> problems = {
        MakeProblem("import", core::ProblemSource::ImportExport, "G-1"),
        MakeProblem("no-element", core::ProblemSource::ModelValidation, ""),
        MakeProblem("review", core::ProblemSource::ReviewComment, "G-2"),
    };

    const std::unordered_set<std::string> attention = core::CollectAttentionElementIds(problems);

    ASSERT_EQ(attention.size(), 1u);
    EXPECT_TRUE(attention.count("G-2") > 0);
}

TEST(ProblemAttentionTest, DeduplicatesMultipleProblemsForSameElement) {
    std::vector<core::ProblemItem> problems = {
        MakeProblem("review", core::ProblemSource::ReviewComment, "G-1"),
        MakeProblem("validation", core::ProblemSource::ModelValidation, "G-1"),
        MakeProblem("guideline", core::ProblemSource::GuidelineReview, "G-1"),
    };

    const std::unordered_set<std::string> attention = core::CollectAttentionElementIds(problems);

    ASSERT_EQ(attention.size(), 1u);
    EXPECT_TRUE(attention.count("G-1") > 0);
}

// --- BuildElementBadgeSummaries / HighestSeverityProblemForElement ---
//
// Intent (from problem_attention.h): reduce a problem snapshot to a per-element
// badge summary whose icon/colour reflect the highest severity, with a stable
// id tie-break, a count, and a separate "highest review-derived problem" so a
// "Go to Review" affordance stays accurate even when a higher-severity
// non-review problem dominates the element.

namespace {

core::ProblemItem MakeSeverityProblem(const std::string& id,
                                      core::ProblemSource source,
                                      const std::string& element_id,
                                      core::ProblemSeverity severity) {
    core::ProblemItem problem = MakeProblem(id, source, element_id);
    problem.severity = severity;
    problem.message = "msg-" + id;
    return problem;
}

} // namespace

TEST(ProblemAttentionTest, BadgeSummaryAggregatesCountAndHighestSeverity) {
    std::vector<core::ProblemItem> problems = {
        MakeSeverityProblem("a-info", core::ProblemSource::ModelValidation, "G-1", core::ProblemSeverity::Info),
        MakeSeverityProblem("b-warn", core::ProblemSource::ModelValidation, "G-1", core::ProblemSeverity::Warning),
        MakeSeverityProblem("c-error", core::ProblemSource::ModelValidation, "G-1", core::ProblemSeverity::Error),
    };

    const auto summaries = core::BuildElementBadgeSummaries(problems);

    ASSERT_EQ(summaries.size(), 1u);
    const core::ElementBadgeSummary& summary = summaries.at("G-1");
    EXPECT_EQ(summary.problem_count, 3u);
    EXPECT_EQ(summary.highest_severity, core::ProblemSeverity::Error);
    EXPECT_EQ(summary.top_problem_id, "c-error");
    EXPECT_EQ(summary.top_problem_message, "msg-c-error");
}

TEST(ProblemAttentionTest, BadgeSummaryBreaksSeverityTiesByLowestId) {
    std::vector<core::ProblemItem> problems = {
        MakeSeverityProblem("b-error", core::ProblemSource::ModelValidation, "G-1", core::ProblemSeverity::Error),
        MakeSeverityProblem("a-error", core::ProblemSource::ModelValidation, "G-1", core::ProblemSeverity::Error),
    };

    const auto summaries = core::BuildElementBadgeSummaries(problems);

    ASSERT_EQ(summaries.size(), 1u);
    EXPECT_EQ(summaries.at("G-1").top_problem_id, "a-error");
}

TEST(ProblemAttentionTest, BadgeSummaryTracksReviewProblemSeparatelyFromOverallTop) {
    // A higher-severity non-review problem dominates the overall summary, while
    // a lower-severity review problem must still be surfaced for "Go to Review".
    std::vector<core::ProblemItem> problems = {
        MakeSeverityProblem("validation-error", core::ProblemSource::ModelValidation, "G-1",
                            core::ProblemSeverity::Error),
        MakeSeverityProblem("review-warn", core::ProblemSource::ReviewComment, "G-1", core::ProblemSeverity::Warning),
    };

    const auto summaries = core::BuildElementBadgeSummaries(problems);

    ASSERT_EQ(summaries.size(), 1u);
    const core::ElementBadgeSummary& summary = summaries.at("G-1");
    EXPECT_EQ(summary.highest_severity, core::ProblemSeverity::Error);
    EXPECT_EQ(summary.top_problem_id, "validation-error");
    EXPECT_TRUE(summary.has_review_problem);
    EXPECT_EQ(summary.top_review_severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(summary.top_review_problem_id, "review-warn");
}

TEST(ProblemAttentionTest, BadgeSummariesSkipNonAttentionProblems) {
    std::vector<core::ProblemItem> problems = {
        MakeSeverityProblem("import", core::ProblemSource::ImportExport, "G-1", core::ProblemSeverity::Error),
        MakeSeverityProblem("global", core::ProblemSource::ModelValidation, "", core::ProblemSeverity::Error),
    };

    EXPECT_TRUE(core::BuildElementBadgeSummaries(problems).empty());
}

TEST(ProblemAttentionTest, HighestSeverityProblemForElementSelectsTopAndHandlesMisses) {
    std::vector<core::ProblemItem> problems = {
        MakeSeverityProblem("warn", core::ProblemSource::ModelValidation, "G-1", core::ProblemSeverity::Warning),
        MakeSeverityProblem("error", core::ProblemSource::ModelValidation, "G-1", core::ProblemSeverity::Error),
        MakeSeverityProblem("other", core::ProblemSource::ModelValidation, "G-2", core::ProblemSeverity::Error),
    };

    const core::ProblemItem* top = core::HighestSeverityProblemForElement(problems, "G-1");
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->id, "error");

    EXPECT_EQ(core::HighestSeverityProblemForElement(problems, "unknown"), nullptr);
    EXPECT_EQ(core::HighestSeverityProblemForElement(problems, ""), nullptr);
}