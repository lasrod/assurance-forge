#include <gtest/gtest.h>

#include "app/controllers/ai_review_controller.h"

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

    ControllerHarness()
        : controller(events, problems, task_runner, nullptr) {
        events.Subscribe<app::StatusMessageEvent>([this](const app::StatusMessageEvent& event) {
            statuses.push_back(event.message);
        });
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

}  // namespace

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

TEST(AiReviewControllerTest, CancelPendingRequestClearsDebugModal) {
    ControllerHarness harness;

    harness.controller.SetDebugModalVisible(true);
    harness.controller.CancelPendingRequest();

    EXPECT_FALSE(harness.controller.ShouldShowDebugModal());
    EXPECT_TRUE(harness.controller.PendingDebugText().empty());
}
