#include "app/controllers/ai_review_controller.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace {

struct ControllerHarness {
    app::AppEvents events;
    core::ProblemsManager problems;
    ai::AiTaskRunner task_runner;
    app::controllers::AiReviewController controller;
    std::vector<std::string> statuses;

    ControllerHarness() : controller(events, problems, task_runner, nullptr) {
        events.Subscribe<app::StatusMessageEvent>(
            [this](const app::StatusMessageEvent& event) { statuses.push_back(event.message); });
    }
};

parser::AssuranceCase MakeCaseWithElement(std::string id, std::string type) {
    parser::AssuranceCase assurance_case;
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    element.name = "Element";
    element.content = "Content";
    assurance_case.elements.push_back(std::move(element));
    return assurance_case;
}

parser::Guideline MakeGuideline(std::string id, std::string category) {
    parser::Guideline guideline;
    guideline.id = std::move(id);
    guideline.category = std::move(category);
    guideline.title = "Guideline";
    guideline.statement = "Statement";
    return guideline;
}

app::GuidelineCatalog MakeCatalog(parser::GuidelinesDocument document) {
    return app::BuildGuidelineCatalog(std::move(document), "sccg.full.yaml");
}

} // namespace

TEST(AiReviewControllerTest, NoSelectionAddsInfoProblemAndStatus) {
    ControllerHarness harness;
    core::AssuranceTree tree;

    harness.controller.BeginReviewForSelection(nullptr, tree, "");

    EXPECT_EQ(harness.statuses.back(), "No GSN element is selected for AI review.");
    std::optional<core::ProblemItem> problem = harness.problems.GetProblemById("ai-review:no-selection");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Info);
    EXPECT_EQ(problem->source, core::ProblemSource::AIReview);
}

TEST(AiReviewControllerTest, NoLoadedCaseAddsErrorProblemAndStatus) {
    ControllerHarness harness;
    core::AssuranceTree tree;

    harness.controller.BeginReviewForSelection(nullptr, tree, "claim-1");

    EXPECT_EQ(harness.statuses.back(), "No assurance case is loaded for AI review.");
    std::optional<core::ProblemItem> problem = harness.problems.GetProblemById("ai-review:claim-1:no-loaded-case");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Error);
    EXPECT_EQ(problem->element_id, "claim-1");
}

TEST(AiReviewControllerTest, MissingSelectedElementAddsProblemAndStatus) {
    ControllerHarness harness;
    core::AssuranceTree tree;
    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-2");

    EXPECT_EQ(harness.statuses.back(), "Selected element was not found.");
    std::optional<core::ProblemItem> problem = harness.problems.GetProblemById("ai-review:claim-2:missing-element");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Error);
}

TEST(AiReviewControllerTest, UnsupportedElementAddsInfoProblemAndStatus) {
    ControllerHarness harness;
    core::AssuranceTree tree;
    parser::AssuranceCase assurance_case = MakeCaseWithElement("artifact-1", "artifactreference");

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "artifact-1");

    EXPECT_EQ(harness.statuses.back(), "AI Review currently supports GSN Goal / SACM Claim elements only.");
    std::optional<core::ProblemItem> problem = harness.problems.GetProblemById("ai-review:artifact-1:unsupported-type");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Info);
    EXPECT_EQ(problem->element_id, "artifact-1");
}

TEST(AiReviewControllerTest, CancelPendingRequestClearsPendingDebugState) {
    ControllerHarness harness;

    harness.controller.SetDebugModalVisible(true);
    harness.controller.SetPendingPrompt("debug prompt");
    harness.controller.CancelPendingRequest();

    EXPECT_FALSE(harness.controller.ShouldShowDebugModal());
    EXPECT_FALSE(harness.controller.HasPendingRequest());
    EXPECT_TRUE(harness.controller.PendingPrompt().empty());
    EXPECT_TRUE(harness.controller.PendingDebugText().empty());
}

TEST(AiReviewControllerTest, SelectClaimReviewGuidelinesUsesProfileWhenAvailable) {
    parser::GuidelinesDocument document;
    document.guidelines.push_back(MakeGuideline("CL.1", "CL"));
    document.guidelines.push_back(MakeGuideline("RD.4", "RD"));
    parser::ReviewProfile profile;
    profile.id = "claim_wording_review";
    profile.guideline_ids = {"RD.4"};
    document.review_profiles.push_back(profile);

    app::GuidelineCatalog catalog = MakeCatalog(std::move(document));
    app::controllers::AiReviewGuidelineSelection selection = app::controllers::SelectClaimReviewGuidelines(catalog);

    ASSERT_NE(selection.review_profile, nullptr);
    ASSERT_EQ(selection.guidelines.size(), 1u);
    EXPECT_EQ(selection.guidelines[0]->id, "RD.4");
    EXPECT_TRUE(selection.error_message.empty());
}

TEST(AiReviewControllerTest, SelectClaimReviewGuidelinesFallsBackToClWhenProfileMissing) {
    parser::GuidelinesDocument document;
    document.guidelines.push_back(MakeGuideline("CL.1", "CL"));
    document.guidelines.push_back(MakeGuideline("RD.4", "RD"));

    app::GuidelineCatalog catalog = MakeCatalog(std::move(document));
    app::controllers::AiReviewGuidelineSelection selection = app::controllers::SelectClaimReviewGuidelines(catalog);

    EXPECT_EQ(selection.review_profile, nullptr);
    ASSERT_EQ(selection.guidelines.size(), 1u);
    EXPECT_EQ(selection.guidelines[0]->id, "CL.1");
    EXPECT_TRUE(selection.error_message.empty());
}

TEST(AiReviewControllerTest, SelectClaimReviewGuidelinesReportsProfileWithNoValidGuidelines) {
    parser::GuidelinesDocument document;
    document.guidelines.push_back(MakeGuideline("CL.1", "CL"));
    parser::ReviewProfile profile;
    profile.id = "claim_wording_review";
    profile.guideline_ids = {"missing-guideline"};
    document.review_profiles.push_back(profile);

    app::GuidelineCatalog catalog = MakeCatalog(std::move(document));
    app::controllers::AiReviewGuidelineSelection selection = app::controllers::SelectClaimReviewGuidelines(catalog);

    ASSERT_NE(selection.review_profile, nullptr);
    EXPECT_TRUE(selection.guidelines.empty());
    EXPECT_NE(selection.error_message.find("no valid guidelines"), std::string::npos);
}

TEST(AiReviewControllerTest, BeginReviewForSelectionBuildsProfilePrompt) {
    ControllerHarness harness;
    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");

    ASSERT_TRUE(harness.controller.HasPendingRequest());
    ASSERT_FALSE(harness.controller.PendingPrompt().empty());
    ASSERT_FALSE(harness.controller.PendingDebugText().empty());
    EXPECT_NE(harness.controller.PendingDebugText().find("claim_wording_review"), std::string::npos);
    EXPECT_EQ(harness.controller.PendingDebugText().find("SCCG CL rules"), std::string::npos);
    EXPECT_FALSE(harness.controller.ShouldShowDebugModal());
    EXPECT_EQ(harness.statuses.back(), "AI review request is ready in the AI Debug panel.");
}
