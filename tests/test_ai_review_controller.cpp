#include "app/controllers/ai_review_controller.h"

#include "ai/ai_provider.h"
#include "ai/secret_store.h"
#include "app/review_problem_sync.h"
#include "core/reviews/review_proposal.h"
#include "review/sccg/sccg_profile_selector.h"

#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct ControllerHarness {
    app::AppEvents events;
    core::ProblemsManager problems;
    app::controllers::ReviewController reviews;
    ai::AiTaskRunner task_runner;
    app::controllers::AiReviewController controller;
    std::vector<std::string> statuses;
    std::vector<app::ElementReviewVisualEvent> review_visual_events;
    std::vector<app::AiReviewProposalSuggestionsEvent> proposal_suggestion_events;

    ControllerHarness() : reviews(events), controller(events, problems, reviews, task_runner, nullptr) {
        events.Subscribe<app::StatusMessageEvent>(
            [this](const app::StatusMessageEvent& event) { statuses.push_back(event.message); });
        events.Subscribe<app::ElementReviewVisualEvent>(
            [this](const app::ElementReviewVisualEvent& event) { review_visual_events.push_back(event); });
        events.Subscribe<app::AiReviewProposalSuggestionsEvent>(
            [this](const app::AiReviewProposalSuggestionsEvent& event) {
                proposal_suggestion_events.push_back(event);
            });
        events.Subscribe<app::ReviewItemsDirtyEvent>(
            [this](const app::ReviewItemsDirtyEvent&) { app::SyncReviewProblems(problems, reviews.Items()); });
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
    std::vector<app::ElementReviewVisualEvent> review_visual_events;
    std::vector<app::AiReviewProposalSuggestionsEvent> proposal_suggestion_events;

    ServiceControllerHarness() : reviews(events), controller(events, problems, reviews, task_runner, service) {
        ai::AiProviderSettings settings;
        settings.enabled = true;
        std::string error;
        service->SaveSettings(settings, error);
        service->SaveApiKey("sk-test");
        events.Subscribe<app::StatusMessageEvent>(
            [this](const app::StatusMessageEvent& event) { statuses.push_back(event.message); });
        events.Subscribe<app::ElementReviewVisualEvent>(
            [this](const app::ElementReviewVisualEvent& event) { review_visual_events.push_back(event); });
        events.Subscribe<app::AiReviewProposalSuggestionsEvent>(
            [this](const app::AiReviewProposalSuggestionsEvent& event) {
                proposal_suggestion_events.push_back(event);
            });
        events.Subscribe<app::ReviewItemsDirtyEvent>(
            [this](const app::ReviewItemsDirtyEvent&) { app::SyncReviewProblems(problems, reviews.Items()); });
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

parser::ReviewProfile
MakeReviewProfile(std::string id, std::string display_name, std::string applies_to, std::string guideline_id) {
    parser::ReviewProfile profile;
    profile.id = std::move(id);
    profile.display_name = std::move(display_name);
    profile.applies_to = {std::move(applies_to)};
    profile.guideline_ids = {std::move(guideline_id)};
    return profile;
}

parser::GuidelinesDocument MakeElementReviewProfiles() {
    parser::GuidelinesDocument document;
    document.guidelines = {
        MakeGuideline("CL.1", "CL"),
        MakeGuideline("AR.1", "AR"),
        MakeGuideline("EV.1", "EV"),
        MakeGuideline("SU.1", "SU"),
        MakeGuideline("SU.2", "SU"),
        MakeGuideline("CL.3", "CL"),
        MakeGuideline("SU.11", "SU"),
    };
    document.review_profiles = {
        MakeReviewProfile("claim_review", "Claim review", "GSN Goal", "CL.1"),
        MakeReviewProfile("strategy_review", "Strategy review", "GSN Strategy", "AR.1"),
        MakeReviewProfile("evidence_review", "Evidence review", "GSN Solution", "EV.1"),
        MakeReviewProfile("assumption_review", "Assumption review", "GSN Assumption", "SU.1"),
        MakeReviewProfile("justification_review", "Justification review", "GSN Justification", "SU.2"),
        MakeReviewProfile("context_review", "Context review", "GSN Context", "CL.3"),
        MakeReviewProfile("challenge_review", "Challenge review", "GSN Counter Claim", "SU.11"),
    };
    return document;
}

struct ReviewProfileSelectionCase {
    const char* raw_type;
    const char* assertion_declaration;
    core::NodeRole role;
    bool counter;
    const char* expected_profile_id;
};

const std::vector<ReviewProfileSelectionCase>& ReviewProfileSelectionCases() {
    static const std::vector<ReviewProfileSelectionCase> cases = {
        {"claim", "asserted", core::NodeRole::Claim, false, "claim_review"},
        {"argumentreasoning", "", core::NodeRole::Strategy, false, "strategy_review"},
        {"artifactreference", "", core::NodeRole::Solution, false, "evidence_review"},
        {"claim", "assumed", core::NodeRole::Assumption, false, "assumption_review"},
        {"claim", "justification", core::NodeRole::Justification, false, "justification_review"},
        {"artifactreference", "", core::NodeRole::Context, false, "context_review"},
        {"claim", "asserted", core::NodeRole::Claim, true, "challenge_review"},
        {"artifactreference", "", core::NodeRole::Solution, true, "challenge_review"},
    };
    return cases;
}

core::GuidelineCatalog MakeCatalog(parser::GuidelinesDocument document) {
    return core::BuildGuidelineCatalog(std::move(document), "sccg.full.yaml");
}

core::ProblemItem MakeManualProblem(const std::string& id, const std::string& element_id) {
    core::ProblemItem problem;
    problem.id = id;
    problem.severity = core::ProblemSeverity::Warning;
    problem.source = core::ProblemSource::Manual;
    problem.element_id = element_id;
    problem.type = "Manual";
    problem.message = "Manual problem";
    return problem;
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

TEST(AiReviewControllerTest, SelectsExactlyOneReviewProfileForEverySupportedGsnElementRole) {
    core::GuidelineCatalog catalog = MakeCatalog(MakeElementReviewProfiles());
    for (const ReviewProfileSelectionCase& selection_case : ReviewProfileSelectionCases()) {
        SCOPED_TRACE(selection_case.expected_profile_id);
        parser::SacmElement element;
        element.id = "selected";
        element.type = selection_case.raw_type;
        element.assertion_declaration = selection_case.assertion_declaration;
        core::TreeNode node;
        node.id = element.id;
        node.role = selection_case.role;
        node.is_counter_source = selection_case.counter;

        review::AiReviewGuidelineSelection selection = review::SelectReviewProfileForElement(catalog, element, &node);

        ASSERT_NE(selection.review_profile, nullptr) << selection.error_message;
        EXPECT_EQ(selection.review_profile->id, selection_case.expected_profile_id);
        EXPECT_EQ(selection.guidelines.size(), 1u);
        EXPECT_TRUE(selection.error_message.empty());
    }
}

TEST(AiReviewControllerTest, SccgReleaseSelectsOneProfileForEverySupportedGsnElementRole) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;
    ASSERT_EQ(catalog.document.sccg_version, "0.6.0");

    for (const ReviewProfileSelectionCase& selection_case : ReviewProfileSelectionCases()) {
        SCOPED_TRACE(selection_case.expected_profile_id);
        parser::SacmElement element;
        element.id = "selected";
        element.type = selection_case.raw_type;
        element.assertion_declaration = selection_case.assertion_declaration;
        core::TreeNode node;
        node.id = element.id;
        node.role = selection_case.role;
        node.is_counter_source = selection_case.counter;

        review::AiReviewGuidelineSelection selection = review::SelectReviewProfileForElement(catalog, element, &node);

        ASSERT_NE(selection.review_profile, nullptr) << selection.error_message;
        EXPECT_EQ(selection.review_profile->id, selection_case.expected_profile_id);
        EXPECT_FALSE(selection.guidelines.empty());
        EXPECT_TRUE(selection.error_message.empty());
    }
}

TEST(AiReviewControllerTest, RefusesAmbiguousElementReviewProfiles) {
    parser::GuidelinesDocument document = MakeElementReviewProfiles();
    document.review_profiles.push_back(MakeReviewProfile("second_claim_review", "Other", "GSN Goal", "CL.1"));
    core::GuidelineCatalog catalog = MakeCatalog(std::move(document));
    parser::SacmElement element = MakeCaseWithElement("G1", "claim").elements.front();
    core::TreeNode node;
    node.id = element.id;
    node.role = core::NodeRole::Claim;

    review::AiReviewGuidelineSelection selection = review::SelectReviewProfileForElement(catalog, element, &node);

    EXPECT_EQ(selection.review_profile, nullptr);
    EXPECT_NE(selection.error_message.find("More than one SCCG review profile"), std::string::npos);
}

TEST(AiReviewControllerTest, ReportsWhenElementHasNoReviewProfile) {
    parser::GuidelinesDocument document = MakeElementReviewProfiles();
    std::erase_if(document.review_profiles,
                  [](const parser::ReviewProfile& profile) { return profile.id == "context_review"; });
    core::GuidelineCatalog catalog = MakeCatalog(std::move(document));
    parser::SacmElement element = MakeCaseWithElement("C1", "artifactreference").elements.front();
    core::TreeNode node;
    node.id = element.id;
    node.role = core::NodeRole::Context;

    review::AiReviewGuidelineSelection selection = review::SelectReviewProfileForElement(catalog, element, &node);

    EXPECT_EQ(selection.review_profile, nullptr);
    EXPECT_NE(selection.error_message.find("No SCCG review profile"), std::string::npos);
}

TEST(AiReviewControllerTest, BeginReviewForSelectionBuildsProfilePrompt) {
    ControllerHarness harness;
    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");

    ASSERT_TRUE(harness.controller.HasPendingRequest());
    ASSERT_FALSE(harness.controller.PendingPrompt().empty());
    ASSERT_FALSE(harness.controller.PendingDebugText().empty());
    EXPECT_NE(harness.controller.PendingDebugText().find("claim_review"), std::string::npos);
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
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();

    std::vector<core::reviews::ReviewItem> comments = harness.reviews.ItemsForElement("claim-1");
    ASSERT_EQ(comments.size(), 1u);
    EXPECT_EQ(comments[0].source, core::reviews::ReviewItemSource::AIReview);
    EXPECT_EQ(comments[0].status, core::reviews::ReviewItemStatus::Open);
    EXPECT_FALSE(comments[0].proposal_id.has_value());
    ASSERT_EQ(comments[0].guideline_ids.size(), 1u);
    EXPECT_EQ(comments[0].guideline_ids[0], "CL.1");
    EXPECT_NE(comments[0].message.find("Suggested claim wording"), std::string::npos);
    ASSERT_GE(harness.review_visual_events.size(), 2u);
    EXPECT_EQ(harness.review_visual_events.front().kind, app::ElementReviewVisualEventKind::AiStarted);
    EXPECT_EQ(harness.review_visual_events.back().kind, app::ElementReviewVisualEventKind::AiFindings);
    EXPECT_EQ(harness.review_visual_events.back().element_id, "claim-1");
    ASSERT_EQ(harness.proposal_suggestion_events.size(), 1u);
    EXPECT_EQ(harness.proposal_suggestion_events[0].review_profile_id, "claim_review");
    EXPECT_EQ(harness.proposal_suggestion_events[0].review_profile_name, "Claim review");
    EXPECT_FALSE(harness.proposal_suggestion_events[0].review_run_id.empty());
    EXPECT_EQ(harness.proposal_suggestion_events[0].reviewed_model_hash,
              core::reviews::ComputeModelSemanticHash(assurance_case));
    ASSERT_EQ(harness.proposal_suggestion_events[0].suggestions.size(), 1u);
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].review_item_id, comments[0].id);
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].element_id, "claim-1");
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].suggested_text,
              "The braking controller response time meets the defined acceptance criterion.");
    core::reviews::ElementReviewState review_state = harness.reviews.ElementReviewStateForElement("claim-1");
    EXPECT_FALSE(review_state.ai_ok);
    EXPECT_FALSE(review_state.failed);
    EXPECT_EQ(review_state.last_review_message, "AI review completed with findings.");
    EXPECT_EQ(harness.statuses.back(), "AI review completed with 1 finding(s) added as review comment(s).");
}

TEST(AiReviewControllerTest, StrategyReviewEmitsProposalSuggestionFromSuggestedElementText) {
    ServiceControllerHarness harness;
    harness.provider->response_text = R"json({
        "reviewed_element_id": "strategy-1",
        "reviewed_element_type": "GSN Strategy / SACM ArgumentReasoning",
        "findings": [
            {
                "source": "SCCG",
                "guideline_id": "AR.2",
                "guideline_title": "State the inference step explicitly",
                "severity": "warning",
                "confidence": "high",
                "message": "The strategy does not explain why the children support the parent.",
                "why_it_matters": "Reviewers need to understand the decomposition rule.",
                "suggested_fix": "State the decomposition basis explicitly.",
                "suggested_element_text": "Argument by credible hazard class, covering blade contact, electrical and thermal hazards, mechanical stability, residual-risk communication, and production conformity.",
                "related_element_ids": ["strategy-1"]
            }
        ]
    })json";

    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeCaseWithElement("goal-1", "claim").elements.front());
    assurance_case.elements.push_back(MakeCaseWithElement("strategy-1", "argumentreasoning").elements.front());
    assurance_case.elements.back().content = "Argument by credible hazard control.";
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "strategy-1");
    ASSERT_TRUE(harness.controller.HasPendingRequest());
    harness.controller.StartPendingRequest();
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();

    std::vector<core::reviews::ReviewItem> comments = harness.reviews.ItemsForElement("strategy-1");
    ASSERT_EQ(comments.size(), 1u);
    ASSERT_EQ(harness.proposal_suggestion_events.size(), 1u);
    ASSERT_EQ(harness.proposal_suggestion_events[0].suggestions.size(), 1u);
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].review_item_id, comments[0].id);
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].element_id, "strategy-1");
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].suggested_text,
              "Argument by credible hazard class, covering blade contact, electrical and thermal hazards, mechanical "
              "stability, residual-risk communication, and production conformity.");
}

TEST(AiReviewControllerTest, EvidenceReviewEmitsProposalSuggestionFromSuggestedElementText) {
    ServiceControllerHarness harness;
    harness.provider->response_text = R"json({
        "reviewed_element_id": "evidence-1",
        "reviewed_element_type": "GSN Solution / SACM ArtifactReference",
        "findings": [
            {
                "source": "SCCG",
                "guideline_id": "EV.2",
                "guideline_title": "Use evidence types that can be independently reviewed",
                "severity": "warning",
                "confidence": "high",
                "message": "The evidence reference is too vague for independent review.",
                "why_it_matters": "Reviewers need a precise artifact reference.",
                "suggested_fix": "Name the specific evidence artifact.",
                "suggested_element_text": "Blade access verification report BAV-01 rev C",
                "related_element_ids": ["evidence-1"]
            }
        ]
    })json";

    parser::AssuranceCase assurance_case = MakeCaseWithElement("evidence-1", "artifactreference");
    assurance_case.elements.front().content.clear();
    assurance_case.elements.front().name = "Evidence";
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "evidence-1");
    ASSERT_TRUE(harness.controller.HasPendingRequest());
    harness.controller.StartPendingRequest();
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();

    std::vector<core::reviews::ReviewItem> comments = harness.reviews.ItemsForElement("evidence-1");
    ASSERT_EQ(comments.size(), 1u);
    ASSERT_EQ(harness.proposal_suggestion_events.size(), 1u);
    ASSERT_EQ(harness.proposal_suggestion_events[0].suggestions.size(), 1u);
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].review_item_id, comments[0].id);
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].element_id, "evidence-1");
    EXPECT_EQ(harness.proposal_suggestion_events[0].suggestions[0].suggested_text,
              "Blade access verification report BAV-01 rev C");
}

TEST(AiReviewControllerTest, StartPendingRequestEmitsRunningVisualEvent) {
    ServiceControllerHarness harness;
    harness.provider->response_text = R"json({"reviewed_element_id":"claim-1","findings":[]})json";

    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");
    ASSERT_TRUE(harness.controller.HasPendingRequest());
    harness.controller.StartPendingRequest();

    ASSERT_FALSE(harness.review_visual_events.empty());
    EXPECT_EQ(harness.review_visual_events.back().kind, app::ElementReviewVisualEventKind::AiStarted);
    EXPECT_EQ(harness.review_visual_events.back().element_id, "claim-1");
    EXPECT_TRUE(harness.review_visual_events.back().review_scope_element_ids.count("claim-1") > 0);
    core::reviews::ElementReviewState review_state = harness.reviews.ElementReviewStateForElement("claim-1");
    EXPECT_FALSE(review_state.ai_ok);
    EXPECT_FALSE(review_state.failed);
    EXPECT_EQ(review_state.last_review_message, "AI review in progress.");
}

TEST(AiReviewControllerTest, RequestFailureEmitsFailedVisualEvent) {
    ControllerHarness harness;
    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");
    ASSERT_TRUE(harness.controller.HasPendingRequest());
    harness.controller.StartPendingRequest();
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();

    ASSERT_GE(harness.review_visual_events.size(), 2u);
    EXPECT_EQ(harness.review_visual_events.front().kind, app::ElementReviewVisualEventKind::AiStarted);
    EXPECT_EQ(harness.review_visual_events.back().kind, app::ElementReviewVisualEventKind::AiFailed);
    EXPECT_EQ(harness.review_visual_events.back().element_id, "claim-1");
    std::vector<core::reviews::ReviewItem> comments = harness.reviews.ItemsForElement("claim-1");
    ASSERT_EQ(comments.size(), 1u);
    EXPECT_EQ(comments[0].source, core::reviews::ReviewItemSource::AIReview);
    EXPECT_NE(comments[0].message.find("AI review request failed"), std::string::npos);
    EXPECT_TRUE(harness.problems.GetProblemById(std::string("review-comment:") + comments[0].id).has_value());
    core::reviews::ElementReviewState review_state = harness.reviews.ElementReviewStateForElement("claim-1");
    EXPECT_FALSE(review_state.ai_ok);
    EXPECT_TRUE(review_state.failed);
    EXPECT_EQ(review_state.last_review_message, "AI review request failed.");
}

TEST(AiReviewControllerTest, ParseFailureEmitsFailedVisualEvent) {
    ServiceControllerHarness harness;
    harness.provider->response_text = "not json";

    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");
    ASSERT_TRUE(harness.controller.HasPendingRequest());
    harness.controller.StartPendingRequest();
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();

    ASSERT_GE(harness.review_visual_events.size(), 2u);
    EXPECT_EQ(harness.review_visual_events.back().kind, app::ElementReviewVisualEventKind::AiFailed);
    EXPECT_EQ(harness.review_visual_events.back().message, "AI review response could not be parsed.");
    std::vector<core::reviews::ReviewItem> comments = harness.reviews.ItemsForElement("claim-1");
    ASSERT_EQ(comments.size(), 1u);
    EXPECT_EQ(comments[0].source, core::reviews::ReviewItemSource::AIReview);
    EXPECT_NE(comments[0].message.find("expected JSON format"), std::string::npos);
    EXPECT_TRUE(harness.problems.GetProblemById(std::string("review-comment:") + comments[0].id).has_value());
}

TEST(AiReviewControllerTest, NoFindingsEmitsAiOkEventAndPreservesUnrelatedProblems) {
    ServiceControllerHarness harness;
    harness.provider->response_text = R"json({
        "reviewed_element_id": "claim-1",
        "reviewed_element_type": "GSN Goal / SACM Claim",
        "findings": []
    })json";
    harness.problems.AddOrUpdateProblem(MakeManualProblem("manual:claim-1", "claim-1"));

    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");
    ASSERT_TRUE(harness.controller.HasPendingRequest());
    harness.controller.StartPendingRequest();
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();

    ASSERT_GE(harness.review_visual_events.size(), 2u);
    EXPECT_EQ(harness.review_visual_events.back().kind, app::ElementReviewVisualEventKind::AiNoFindings);
    EXPECT_EQ(harness.review_visual_events.back().element_id, "claim-1");
    EXPECT_TRUE(harness.problems.GetProblemById("manual:claim-1").has_value());
    core::reviews::ElementReviewState review_state = harness.reviews.ElementReviewStateForElement("claim-1");
    EXPECT_TRUE(review_state.ai_ok);
    EXPECT_FALSE(review_state.failed);
    EXPECT_EQ(review_state.last_review_message, "AI review completed with no findings.");
    EXPECT_EQ(harness.statuses.back(), "AI review completed with no findings.");
}
