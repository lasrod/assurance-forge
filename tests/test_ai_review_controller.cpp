#include "app/controllers/ai_review_controller.h"

#include "ai/ai_provider.h"
#include "ai/secret_store.h"
#include "app/review_problem_sync.h"
#include "core/guideline_catalog.h"
#include "core/reviews/review_proposal.h"
#include "review/sccg/sccg_profile_selector.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
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
    // Requests run concurrently on the task runner's threads, one per review
    // pass, so the count is atomic.
    std::atomic<int> calls{0};
    // Fail any request whose prompt contains this, to fail one pass of several.
    std::string fail_when_prompt_contains;

    ai::AiProviderId ProviderId() const override {
        return ai::AiProviderId::OpenAI;
    }

    ai::AiConnectionStatus TestConnection(const ai::AiProviderSettings&, const std::string&) override {
        return ai::SuccessStatus("ok");
    }

    ai::AiResponse Generate(const ai::AiProviderSettings&, const ai::AiRequest& request, const std::string&) override {
        calls.fetch_add(1);
        ai::AiResponse response;
        if (!fail_when_prompt_contains.empty() &&
            request.userPrompt.find(fail_when_prompt_contains) != std::string::npos) {
            response.success = false;
            response.errorCode = ai::AiErrorCode::Timeout;
            response.errorMessage = "simulated timeout";
            return response;
        }
        response.success = true;
        response.text = response_text;
        return response;
    }
};

// A settings file of the harness's own. A default-constructed AiSettingsStore
// resolves to the developer's real %APPDATA% (or XDG) settings, and the harness
// saves into it -- so every run of this suite used to overwrite the model the
// developer had chosen with the compiled-in default, and switch AI on.
std::filesystem::path HarnessSettingsPath() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("assurance_forge_ai_review_controller_test_" + std::to_string(stamp)) / "settings.json";
}

struct ServiceControllerHarness {
    app::AppEvents events;
    core::ProblemsManager problems;
    app::controllers::ReviewController reviews;
    ai::AiTaskRunner task_runner;
    std::shared_ptr<FakeSecretStore> secret_store = std::make_shared<FakeSecretStore>();
    std::shared_ptr<FixedResponseProvider> provider = std::make_shared<FixedResponseProvider>();
    std::filesystem::path settings_path = HarnessSettingsPath();
    std::shared_ptr<ai::AiSettingsStore> settings_store = std::make_shared<ai::AiSettingsStore>(settings_path);
    std::shared_ptr<ai::AiService> service = std::make_shared<ai::AiService>(settings_store, secret_store, provider);
    app::controllers::AiReviewController controller;
    std::vector<std::string> statuses;
    std::vector<app::ElementReviewVisualEvent> review_visual_events;
    std::vector<app::AiReviewProposalSuggestionsEvent> proposal_suggestion_events;

    ~ServiceControllerHarness() {
        std::error_code error;
        std::filesystem::remove_all(settings_path.parent_path(), error);
    }

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

std::string SelectedElementPackageId(const std::string& element_role) {
    std::string id = "SELECTED_" + element_role;
    for (char& character : id) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return id;
}

// A profile the way SCCG publishes one since 0.7.0: it names the element role it
// reviews by requiring that role's selected-element data package, rather than by
// listing notation element names.
parser::ReviewProfile
MakeReviewProfile(std::string id, std::string display_name, const std::string& element_role, std::string guideline_id) {
    parser::ReviewProfile profile;
    profile.id = std::move(id);
    profile.display_name = std::move(display_name);
    profile.guideline_ids = {std::move(guideline_id)};
    profile.required_data = {SelectedElementPackageId(element_role)};
    return profile;
}

parser::DataPackage MakeSelectedElementPackage(const std::string& element_role) {
    parser::DataPackage package;
    package.id = SelectedElementPackageId(element_role);
    package.display_name = package.id;
    package.role = "selected_element";
    package.element_role = element_role;
    return package;
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
        MakeReviewProfile("claim_review", "Claim review", "claim", "CL.1"),
        MakeReviewProfile("strategy_review", "Strategy review", "strategy", "AR.1"),
        MakeReviewProfile("evidence_review", "Evidence review", "evidence", "EV.1"),
        MakeReviewProfile("assumption_review", "Assumption review", "assumption", "SU.1"),
        MakeReviewProfile("justification_review", "Justification review", "justification", "SU.2"),
        MakeReviewProfile("context_review", "Context review", "context", "CL.3"),
        MakeReviewProfile("challenge_review", "Challenge review", "challenge", "SU.11"),
    };
    for (const char* element_role :
         {"claim", "strategy", "evidence", "assumption", "justification", "context", "challenge"}) {
        document.data_packages.push_back(MakeSelectedElementPackage(element_role));
    }
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
    return core::BuildGuidelineCatalog(std::move(document), "sccg.full.json");
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
    ASSERT_EQ(catalog.document.sccg_version, "0.9.0");

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
    document.review_profiles.push_back(MakeReviewProfile("second_claim_review", "Other", "claim", "CL.1"));
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
    // The scope, not the whole model: an edit outside what this review read must
    // not invalidate it.
    EXPECT_FALSE(harness.proposal_suggestion_events[0].reviewed_element_ids.empty());
    EXPECT_EQ(harness.proposal_suggestion_events[0].reviewed_scope_hash,
              core::reviews::ComputeScopeSemanticHash(assurance_case,
                                                      harness.proposal_suggestion_events[0].reviewed_element_ids));
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

namespace {

constexpr const char* kClaimFindingResponse = R"json({
    "reviewed_element_id": "claim-1",
    "reviewed_element_type": "GSN Goal / SACM Claim",
    "findings": [
        {
            "source": "SCCG",
            "guideline_id": "CL.1",
            "guideline_title": "Write each claim as a falsifiable proposition",
            "confidence": "high",
            "message": "The claim is too vague to falsify.",
            "why_it_matters": "Reviewers need a testable proposition.",
            "suggested_fix": "Rewrite the claim as a measurable statement.",
            "related_element_ids": ["claim-1"]
        }
    ]
})json";

const parser::ReviewProfile& ReleasedClaimReview() {
    static const core::GuidelineCatalog catalog = [] {
        core::GuidelineCatalog loaded;
        std::string error;
        EXPECT_TRUE(core::LoadGuidelineCatalog(loaded, error)) << error;
        return loaded;
    }();
    const parser::ReviewProfile* profile = catalog.document.FindReviewProfileById("claim_review");
    EXPECT_NE(profile, nullptr);
    return *profile;
}

} // namespace

// SCCG 0.8.0 publishes claim_review as four review passes, and a claim review is
// sent as one request per pass. The canned response cites CL.1 to every pass;
// only the pass that owns CL.1 may keep it, so the reviewer sees it once.
TEST(AiReviewControllerTest, ClaimReviewIsSentAsOneRequestPerReviewPass) {
    const parser::ReviewProfile& claim_review = ReleasedClaimReview();
    ASSERT_GT(claim_review.review_passes.size(), 1u);

    ServiceControllerHarness harness;
    harness.provider->response_text = kClaimFindingResponse;
    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");
    harness.controller.StartPendingRequest();
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();

    EXPECT_EQ(harness.provider->calls.load(), static_cast<int>(claim_review.review_passes.size()));
    const std::vector<core::reviews::ReviewItem> comments = harness.reviews.ItemsForElement("claim-1");
    ASSERT_EQ(comments.size(), 1u) << "CL.1 is the wording pass's; the other passes' copies are discarded";
    ASSERT_EQ(comments[0].guideline_ids.size(), 1u);
    EXPECT_EQ(comments[0].guideline_ids[0], "CL.1");
    const core::reviews::ElementReviewState state = harness.reviews.ElementReviewStateForElement("claim-1");
    EXPECT_FALSE(state.failed);
    EXPECT_EQ(harness.review_visual_events.back().kind, app::ElementReviewVisualEventKind::AiFindings);
}

// One pass failing must not read as one pass finding nothing. The findings of
// the passes that ran are still recorded; the review is reported incomplete and
// failed, so it can never earn the no-findings badge.
TEST(AiReviewControllerTest, AReviewWithAFailedPassIsReportedIncomplete) {
    const parser::ReviewProfile& claim_review = ReleasedClaimReview();
    ASSERT_GT(claim_review.review_passes.size(), 1u);
    const parser::ReviewPass& failing = claim_review.review_passes.back();
    ASSERT_EQ(std::find(failing.guideline_ids.begin(), failing.guideline_ids.end(), "CL.1"),
              failing.guideline_ids.end())
        << "the failing pass must not be the one that owns the canned finding";

    ServiceControllerHarness harness;
    harness.provider->response_text = kClaimFindingResponse;
    // The phrase only the failing pass's own framing contains. Every pass
    // request lists all the profile's passes and their questions, so the
    // question alone would match -- and fail -- all of them.
    harness.provider->fail_when_prompt_contains = "It asks: " + failing.question;
    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");
    harness.controller.StartPendingRequest();
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();

    const std::vector<core::reviews::ReviewItem> items = harness.reviews.ItemsForElement("claim-1");
    bool has_finding = false;
    bool has_incomplete_notice = false;
    for (const core::reviews::ReviewItem& item : items) {
        has_finding = has_finding || (!item.guideline_ids.empty() && item.guideline_ids[0] == "CL.1");
        if (item.title == "AI review incomplete") {
            has_incomplete_notice = true;
            EXPECT_NE(item.message.find("simulated timeout"), std::string::npos) << item.message;
            EXPECT_NE(item.message.find(failing.id), std::string::npos) << item.message;
        }
    }
    EXPECT_TRUE(has_finding) << "the passes that ran still report";
    EXPECT_TRUE(has_incomplete_notice);

    const core::reviews::ElementReviewState state = harness.reviews.ElementReviewStateForElement("claim-1");
    EXPECT_TRUE(state.failed);
    EXPECT_FALSE(state.ai_ok);
    EXPECT_EQ(harness.review_visual_events.back().kind, app::ElementReviewVisualEventKind::AiFailed);
}

// The AI Debug panel edits one prompt. For a pass review it shows every pass
// under a separator; an edit that removes the separators cannot be attributed to
// a pass, so it is sent as written -- one request -- and the user is told.
TEST(AiReviewControllerTest, DebugPromptEditThatLosesThePassSeparatorsIsSentAsOneRequest) {
    ServiceControllerHarness harness;
    harness.provider->response_text = kClaimFindingResponse;
    parser::AssuranceCase assurance_case = MakeCaseWithElement("claim-1", "claim");
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    harness.controller.BeginReviewForSelection(&assurance_case, tree, "claim-1");
    const std::string combined = harness.controller.PendingPrompt();
    EXPECT_NE(combined.find("===== SCCG review pass 1/"), std::string::npos);

    harness.controller.SetPendingPrompt("A prompt the user rewrote from scratch.");
    EXPECT_EQ(harness.controller.PendingPrompt(), "A prompt the user rewrote from scratch.");
    EXPECT_NE(harness.statuses.back().find("sent as one request"), std::string::npos) << harness.statuses.back();

    harness.controller.StartPendingRequest();
    ASSERT_TRUE(harness.controller.WaitForCompletion(std::chrono::seconds(10)));
    harness.controller.PollTask();
    EXPECT_EQ(harness.provider->calls.load(), 1);
}
