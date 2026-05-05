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