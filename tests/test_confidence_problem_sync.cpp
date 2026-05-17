#include "app/confidence_problem_sync.h"

#include "core/confidence/confidence_store.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>

namespace {

parser::SacmElement MakeClaim(std::string id = "claim-1", std::string gid = "G1") {
    parser::SacmElement element;
    element.id = std::move(id);
    element.gid = std::move(gid);
    element.type = "claim";
    element.name = "Claim";
    element.content = "Claim text";
    return element;
}

core::confidence::ConfidenceAssessment MakeAssessment(const parser::SacmElement& element, bool stale) {
    core::confidence::FixedConfidenceValue fixed;
    fixed.value = 0.8;

    core::confidence::ConfidenceAssessment assessment;
    assessment.id = "conf-000001";
    assessment.target.kind = core::confidence::ConfidenceTargetKind::Element;
    assessment.target.sourceId = "main";
    assessment.target.sacmGid = element.gid;
    assessment.target.sacmType = core::confidence::DisplaySacmType(element);
    assessment.method = core::confidence::ConfidenceMethod::FixedValue;
    assessment.fixedValue = fixed;
    assessment.derived = core::confidence::DerivedFor(fixed);
    assessment.status = stale ? core::confidence::ConfidenceStatus::Inactive : core::confidence::ConfidenceStatus::Active;
    assessment.stale = stale;
    return assessment;
}

} // namespace

TEST(ConfidenceProblemSyncTest, StaleConfidenceCreatesElementScopedProblem) {
    parser::SacmElement claim = MakeClaim();
    parser::AssuranceCase model;
    model.elements.push_back(claim);

    core::confidence::ConfidenceStore store;
    store.assessments.push_back(MakeAssessment(claim, true));

    core::ProblemsManager problems;
    app::SyncConfidenceProblems(problems, &model, &store, "main");

    ASSERT_EQ(problems.GetProblems().size(), 1u);
    std::optional<core::ProblemItem> problem = problems.GetProblemById("confidence-review:conf-000001");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(problem->source, core::ProblemSource::ModelValidation);
    EXPECT_EQ(problem->element_id, "claim-1");
    EXPECT_EQ(problem->type, "ConfidenceReviewRequired");
}

TEST(ConfidenceProblemSyncTest, CurrentInactiveConfidenceDoesNotCreateProblem) {
    parser::SacmElement claim = MakeClaim();
    parser::AssuranceCase model;
    model.elements.push_back(claim);

    core::confidence::ConfidenceStore store;
    core::confidence::ConfidenceAssessment assessment = MakeAssessment(claim, false);
    assessment.status = core::confidence::ConfidenceStatus::Inactive;
    store.assessments.push_back(assessment);

    core::ProblemsManager problems;
    app::SyncConfidenceProblems(problems, &model, &store, "main");

    EXPECT_TRUE(problems.GetProblems().empty());
}

TEST(ConfidenceProblemSyncTest, SyncClearsStaleConfidenceProblemsOnly) {
    parser::SacmElement claim = MakeClaim();
    parser::AssuranceCase model;
    model.elements.push_back(claim);

    core::confidence::ConfidenceStore store;
    store.assessments.push_back(MakeAssessment(claim, false));

    core::ProblemsManager problems;
    core::ProblemItem unrelated;
    unrelated.id = "manual-1";
    unrelated.source = core::ProblemSource::Manual;
    unrelated.element_id = "claim-2";
    problems.AddProblem(unrelated);
    problems.AddProblem(app::MakeProblemFromStaleConfidence(claim, MakeAssessment(claim, true)));

    app::SyncConfidenceProblems(problems, &model, &store, "main");

    ASSERT_EQ(problems.GetProblems().size(), 1u);
    EXPECT_TRUE(problems.GetProblemById("manual-1").has_value());
    EXPECT_FALSE(problems.GetProblemById("confidence-review:conf-000001").has_value());
}
