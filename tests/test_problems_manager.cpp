#include <gtest/gtest.h>

#include "core/problems/problems_manager.h"

namespace {

core::ProblemItem MakeProblem(const std::string& id,
                              core::ProblemSeverity severity,
                              core::ProblemSource source,
                              const std::string& element_id) {
    core::ProblemItem problem;
    problem.id = id;
    problem.severity = severity;
    problem.source = source;
    problem.element_id = element_id;
    problem.type = "Claim";
    problem.message = "Example problem";
    problem.guideline_id = "SCCG-TEST";
    return problem;
}

}  // namespace

TEST(ProblemsManagerTest, AddsAndFindsProblems) {
    core::ProblemsManager manager;
    manager.AddProblem(MakeProblem("p1", core::ProblemSeverity::Warning, core::ProblemSource::GuidelineReview, "G-1"));

    ASSERT_EQ(manager.GetProblems().size(), 1u);
    auto problem = manager.GetProblemById("p1");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->element_id, "G-1");
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Warning);
}

TEST(ProblemsManagerTest, AddProblemKeepsIdsUnique) {
    core::ProblemsManager manager;
    manager.AddProblem(MakeProblem("p1", core::ProblemSeverity::Info, core::ProblemSource::Manual, "G-1"));
    manager.AddProblem(MakeProblem("p1", core::ProblemSeverity::Warning, core::ProblemSource::GuidelineReview, "G-2"));

    ASSERT_EQ(manager.GetProblems().size(), 1u);
    auto problem = manager.GetProblemById("p1");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(problem->source, core::ProblemSource::GuidelineReview);
    EXPECT_EQ(problem->element_id, "G-2");
}

TEST(ProblemsManagerTest, AddOrUpdateReplacesProblemWithSameId) {
    core::ProblemsManager manager;
    manager.AddOrUpdateProblem(MakeProblem("p1", core::ProblemSeverity::Info, core::ProblemSource::Manual, "G-1"));

    core::ProblemItem updated = MakeProblem("p1", core::ProblemSeverity::Error, core::ProblemSource::ImportExport, "A-4");
    updated.message = "Relationship target is missing";
    manager.AddOrUpdateProblem(updated);

    ASSERT_EQ(manager.GetProblems().size(), 1u);
    auto problem = manager.GetProblemById("p1");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Error);
    EXPECT_EQ(problem->source, core::ProblemSource::ImportExport);
    EXPECT_EQ(problem->element_id, "A-4");
    EXPECT_EQ(problem->message, "Relationship target is missing");
}

TEST(ProblemsManagerTest, IgnoresProblemsWithoutIds) {
    core::ProblemsManager manager;
    manager.AddProblem(MakeProblem("", core::ProblemSeverity::Warning, core::ProblemSource::Manual, "G-1"));

    EXPECT_TRUE(manager.GetProblems().empty());
}

TEST(ProblemsManagerTest, RemovesProblemById) {
    core::ProblemsManager manager;
    manager.AddProblem(MakeProblem("p1", core::ProblemSeverity::Info, core::ProblemSource::Manual, "G-1"));
    manager.AddProblem(MakeProblem("p2", core::ProblemSeverity::Warning, core::ProblemSource::Manual, "G-2"));

    manager.RemoveProblem("p1");

    ASSERT_EQ(manager.GetProblems().size(), 1u);
    EXPECT_FALSE(manager.GetProblemById("p1").has_value());
    EXPECT_TRUE(manager.GetProblemById("p2").has_value());
}

TEST(ProblemsManagerTest, ClearsProblemsBySource) {
    core::ProblemsManager manager;
    manager.AddProblem(MakeProblem("manual", core::ProblemSeverity::Info, core::ProblemSource::Manual, "G-1"));
    manager.AddProblem(MakeProblem("guideline", core::ProblemSeverity::Warning, core::ProblemSource::GuidelineReview, "G-2"));
    manager.AddProblem(MakeProblem("validation", core::ProblemSeverity::Error, core::ProblemSource::ModelValidation, "G-3"));

    manager.ClearProblemsBySource(core::ProblemSource::GuidelineReview);

    ASSERT_EQ(manager.GetProblems().size(), 2u);
    EXPECT_TRUE(manager.GetProblemById("manual").has_value());
    EXPECT_FALSE(manager.GetProblemById("guideline").has_value());
    EXPECT_TRUE(manager.GetProblemById("validation").has_value());
}

TEST(ProblemsManagerTest, ClearsProblemsForElement) {
    core::ProblemsManager manager;
    manager.AddProblem(MakeProblem("p1", core::ProblemSeverity::Info, core::ProblemSource::Manual, "G-1"));
    manager.AddProblem(MakeProblem("p2", core::ProblemSeverity::Warning, core::ProblemSource::GuidelineReview, "G-1"));
    manager.AddProblem(MakeProblem("p3", core::ProblemSeverity::Error, core::ProblemSource::ImportExport, "A-4"));

    manager.ClearProblemsForElement("G-1");

    ASSERT_EQ(manager.GetProblems().size(), 1u);
    EXPECT_FALSE(manager.GetProblemById("p1").has_value());
    EXPECT_FALSE(manager.GetProblemById("p2").has_value());
    EXPECT_TRUE(manager.GetProblemById("p3").has_value());
}

TEST(ProblemsManagerTest, ClearsAllProblems) {
    core::ProblemsManager manager;
    manager.AddProblem(MakeProblem("p1", core::ProblemSeverity::Info, core::ProblemSource::Manual, "G-1"));
    manager.AddProblem(MakeProblem("p2", core::ProblemSeverity::Warning, core::ProblemSource::GuidelineReview, "G-2"));

    manager.ClearProblems();

    EXPECT_TRUE(manager.GetProblems().empty());
    EXPECT_FALSE(manager.GetProblemById("p1").has_value());
}
