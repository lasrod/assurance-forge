#include "app/controllers/ai_review_controller.h"

#include "ai/ai_provider.h"
#include "ai/secret_store.h"

#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct ControllerHarness {
    app::AppEvents events;
    core::ProblemsManager problems;
    app::controllers::ReviewController reviews;
    ai::AiTaskRunner task_runner;
    app::controllers::AiReviewController controller;
    std::vector<std::string> statuses;

    ControllerHarness() : reviews(events), controller(events, problems, reviews, task_runner, nullptr) {
        events.Subscribe<app::StatusMessageEvent>(
            [this](const app::StatusMessageEvent& event) { statuses.push_back(event.message); });
    }
};

class FakeSecretStore final : public ai::ISecretStore {
public:
    std::map<std::string, std::string> secrets;

    bool IsAvailable() const override {
        return true;
    }

    ai::SecretStoreResult
    SaveSecret(const std::string& service, const std::string& account, const std::string& secret) override {
        secrets[service + ":" + account] = secret;
        return ai::SecretStoreSuccess();
    }

    ai::SecretLoadResult LoadSecret(const std::string& service, const std::string& account) override {
        auto found = secrets.find(service + ":" + account);
        if (found == secrets.end())
            return ai::SecretLoadSuccess(std::nullopt);
        return ai::SecretLoadSuccess(found->second);
    }

    ai::SecretStoreResult DeleteSecret(const std::string& service, const std::string& account) override {
        secrets.erase(service + ":" + account);
        return ai::SecretStoreSuccess();
    }
};

class FixedResponseProvider final : public ai::IAiProvider {
public:
    std::string response_text;

    ai::AiProviderId ProviderId() const override {
        return ai::AiProviderId::OpenAI;
    }

    ai::AiConnectionStatus TestConnection(const ai::AiProviderSettings&, const std::string&) override {
        return ai::SuccessStatus("ok");
    }

    ai::AiResponse Generate(const ai::AiProviderSettings&, const ai::AiRequest&, const std::string&) override {
        ai::AiResponse response;
        response.success = true;
        response.text = response_text;
        return response;
    }
};

struct ServiceControllerHarness {
    app::AppEvents events;
    core::ProblemsManager problems;
    app::controllers::ReviewController reviews;
    ai::AiTaskRunner task_runner;
    std::shared_ptr<FakeSecretStore> secret_store = std::make_shared<FakeSecretStore>();
    std::shared_ptr<FixedResponseProvider> provider = std::make_shared<FixedResponseProvider>();
    std::shared_ptr<ai::AiSettingsStore> settings_store = std::make_shared<ai::AiSettingsStore>();
    std::shared_ptr<ai::AiService> service = std::make_shared<ai::AiService>(settings_store, secret_store, provider);
    app::controllers::AiReviewController controller;
    std::vector<std::string> statuses;

    ServiceControllerHarness() : reviews(events), controller(events, problems, reviews, task_runner, service) {
        ai::AiProviderSettings settings;
        settings.enabled = true;
        std::string error;
        service->SaveSettings(settings, error);
        service->SaveApiKey("sk-test");
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
    parser::AssuranceCase assurance_case = MakeCaseWithElement("activity-1", "activity");

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "activity-1");

    EXPECT_EQ(harness.statuses.back(), "AI Review does not support the selected element type.");
    std::optional<core::ProblemItem> problem = harness.problems.GetProblemById("ai-review:activity-1:unsupported-type");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Info);
    EXPECT_EQ(problem->element_id, "activity-1");
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
    EXPECT_NE(harness.controller.PendingDebugText().find("Available data packages"), std::string::npos);
    EXPECT_FALSE(harness.controller.ShouldShowDebugModal());
    EXPECT_EQ(harness.statuses.back(), "AI review request is ready in the AI Debug panel.");
}

TEST(AiReviewControllerTest, CompletedAiFindingsAreAddedAsReviewComments) {
    ServiceControllerHarness harness;
    harness.provider->response_text = R"json({
        "reviewed_element_id": "claim-1",
        "reviewed_element_type": "GSN Goal / SACM Claim",
        "findings": [
            {
                "source": "SCCG",
                "guideline_id": "CL.1",
                "guideline_title": "Write each claim as a falsifiable proposition",
                "severity": "warning",
                "confidence": "high",
                "message": "The claim is too vague to falsify.",
                "why_it_matters": "Reviewers need a testable proposition.",
                "suggested_fix": "Rewrite the claim as a measurable statement.",
                "suggested_claim_wording": "The braking controller response time meets the defined acceptance criterion.",
                "related_element_ids": ["claim-1"]
            }
        ]
    })json";

    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");
    ASSERT_TRUE(harness.controller.HasPendingRequest());
    harness.controller.StartPendingRequest();
    for (int attempt = 0; attempt < 100 && harness.controller.IsReviewRunning(); ++attempt) {
        harness.controller.PollTask();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    harness.controller.PollTask();

    std::vector<core::reviews::ReviewItem> comments = harness.reviews.ItemsForElement("claim-1");
    ASSERT_EQ(comments.size(), 1u);
    EXPECT_EQ(comments[0].source, core::reviews::ReviewItemSource::AIReview);
    EXPECT_EQ(comments[0].status, core::reviews::ReviewItemStatus::Open);
    EXPECT_FALSE(comments[0].proposal_id.has_value());
    ASSERT_EQ(comments[0].guideline_ids.size(), 1u);
    EXPECT_EQ(comments[0].guideline_ids[0], "CL.1");
    EXPECT_NE(comments[0].message.find("Suggested claim wording"), std::string::npos);
    EXPECT_EQ(harness.statuses.back(), "AI review completed with 1 finding(s) added as review comment(s).");
}
