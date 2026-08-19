#include "app/actions/ai_review_actions.h"
#include "app/actions/proposal_actions.h"
#include "app/app_runtime_state.h"
#include "core/guideline_catalog.h"
#include "core/reviews/review_proposal.h"
#include "parser/model_utils.h"
#include "ui/ui_state.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

struct TempDir {
    std::filesystem::path path;

    explicit TempDir(std::filesystem::path value) : path(std::move(value)) {}
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

TempDir MakeTempDir(const std::string& stem) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_ai_review_actions_" + stem + "_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return TempDir(path);
}

parser::AssuranceCase MakeAcceptedCase() {
    parser::AssuranceCase model;
    parser::SacmElement goal;
    goal.id = "G1";
    goal.type = "claim";
    goal.name = "Top goal";
    goal.content = "ACCEPTED_BASELINE_WORDING";
    goal.assertion_declaration = "asserted";
    model.elements.push_back(std::move(goal));
    return model;
}

core::GuidelineCatalog MakeClaimReviewCatalog() {
    parser::GuidelinesDocument document;
    document.sccg_version = "0.6.0";
    parser::Guideline guideline;
    guideline.id = "CL.1";
    guideline.title = "State the claim clearly";
    guideline.category = "CL";
    guideline.statement = "State one clear claim.";
    document.guidelines.push_back(std::move(guideline));
    parser::ReviewProfile profile;
    profile.id = "claim_review";
    profile.display_name = "Claim review";
    profile.description = "Review a goal claim.";
    profile.applies_to = {"GSN Goal"};
    profile.guideline_ids = {"CL.1"};
    document.review_profiles.push_back(std::move(profile));
    return core::BuildGuidelineCatalog(std::move(document), "test-sccg.yaml");
}

parser::AssuranceCase MakeAcceptedCaseWithASecondBranch() {
    parser::AssuranceCase model = MakeAcceptedCase();
    parser::SacmElement other;
    other.id = "G2";
    other.type = "claim";
    other.name = "Unrelated goal";
    other.content = "UNRELATED_BASELINE_WORDING";
    other.assertion_declaration = "asserted";
    model.elements.push_back(std::move(other));
    return model;
}

// An edit by somebody else -- MCP here, but the user's own hand edits land in
// the same draft -- to whichever element is named.
std::string StageDraftEditTo(app::AppRuntimeState& state,
                             const parser::AssuranceCase& accepted,
                             const std::string& element_id,
                             const std::string& old_text,
                             const std::string& new_text) {
    core::drafts::DraftGroupRequest request;
    request.title = "Someone else's edit";
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = "Test MCP";
    std::string error;
    const std::string group_id = state.draft_workspace.BeginGroup(request, accepted, error);
    EXPECT_FALSE(group_id.empty()) << error;

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{element_id, std::nullopt};
    update.field = "content";
    update.old_value = old_text;
    update.new_value = new_text;
    EXPECT_TRUE(state.draft_workspace.StageOperations(group_id, {update}, accepted, error)) << error;
    EXPECT_TRUE(state.draft_workspace.MarkGroupReady(group_id, error)) << error;
    return group_id;
}

std::string StageExistingDraftEdit(app::AppRuntimeState& state, const parser::AssuranceCase& accepted) {
    core::drafts::DraftGroupRequest request;
    request.title = "MCP wording";
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = "Test MCP";
    std::string error;
    const std::string group_id = state.draft_workspace.BeginGroup(request, accepted, error);
    EXPECT_FALSE(group_id.empty()) << error;

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    update.element = core::reviews::ElementRef{"G1", std::nullopt};
    update.field = "content";
    update.old_value = "ACCEPTED_BASELINE_WORDING";
    update.new_value = "UNACCEPTED_WORKING_DRAFT_WORDING";
    EXPECT_TRUE(state.draft_workspace.StageOperations(group_id, {update}, accepted, error)) << error;
    EXPECT_TRUE(state.draft_workspace.MarkGroupReady(group_id, error)) << error;
    return group_id;
}

void OpenDraftStore(app::AppRuntimeState& state, const TempDir& temp, const parser::AssuranceCase& accepted) {
    state.draft_workspace.SetProjectRoot(temp.path);
    std::string error;
    ASSERT_TRUE(state.draft_workspace.Open("argument.sacm", accepted, error)) << error;
}

core::reviews::ReviewItem MakeReviewItem() {
    core::reviews::ReviewItem item;
    item.id = "ai-review-G1-claim_review-1";
    item.element_id = "G1";
    item.title = "AI review finding: CL.1";
    item.message = "The goal should be more precise.";
    item.guideline_ids = {"CL.1"};
    item.source = core::reviews::ReviewItemSource::AIReview;
    return item;
}

} // namespace

TEST(AiReviewActionsTest, DoesNotBuildReviewInputWithoutALoadedCase) {
    app::AppRuntimeState state;
    ui::GetUiState().selected_element_id = "G1";

    app::actions::AiReviewActions(state).BeginForSelection();

    EXPECT_FALSE(state.ai.review_controller->HasPendingRequest());
    EXPECT_TRUE(state.problems_manager.GetProblems().empty());
    ui::GetUiState().selected_element_id.clear();
}

TEST(AiReviewActionsTest, BuildsReviewFromTheMaterializedWorkingDraft) {
    TempDir temp = MakeTempDir("working_view");
    app::AppRuntimeState state;
    state.app_state.loaded_case = MakeAcceptedCase();
    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    StageExistingDraftEdit(state, *state.app_state.loaded_case);
    state.guideline_catalog = MakeClaimReviewCatalog();
    ui::GetUiState().selected_element_id = "G1";

    app::actions::AiReviewActions(state).BeginForSelection();

    ASSERT_TRUE(state.ai.review_controller->HasPendingRequest());
    EXPECT_NE(state.ai.review_controller->PendingPrompt().find("UNACCEPTED_WORKING_DRAFT_WORDING"), std::string::npos);
    EXPECT_EQ(state.ai.review_controller->PendingPrompt().find("ACCEPTED_BASELINE_WORDING"), std::string::npos);
    EXPECT_NE(state.ai.review_controller->PendingDebugText().find("unaccepted working-draft content"),
              std::string::npos);
    ui::GetUiState().selected_element_id.clear();
}

TEST(AiReviewActionsTest, SuggestedTextBecomesAReadySccgDraftGroupAgainstTheReviewedWorkingModel) {
    TempDir temp = MakeTempDir("suggestion");
    app::AppRuntimeState state;
    state.app_state.loaded_case = MakeAcceptedCase();
    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    StageExistingDraftEdit(state, *state.app_state.loaded_case);
    ASSERT_TRUE(state.review_controller->AddOrUpdateItem(MakeReviewItem()));

    const core::drafts::DraftMaterializationResult& before =
        state.draft_workspace.Materialize(*state.app_state.loaded_case, state.app_state.case_revision);
    ASSERT_TRUE(before.success) << before.error;

    app::AiReviewProposalSuggestionsEvent event;
    event.review_profile_id = "claim_review";
    event.review_profile_name = "Claim review";
    event.review_run_id = "run-42";
    event.reviewed_element_ids = {"G1"};
    event.reviewed_scope_hash = core::reviews::ComputeScopeSemanticHash(before.working_model, {"G1"});
    event.suggestions.push_back({"ai-review-G1-claim_review-1", "G1", "AI_SUGGESTED_WORKING_DRAFT_WORDING"});

    app::actions::ProposalActions(state).CreateAiGenerated(event);

    const core::drafts::DraftWorkspace* workspace = state.draft_workspace.workspace();
    ASSERT_NE(workspace, nullptr);
    ASSERT_EQ(workspace->ActiveGroups().size(), 2u);
    const core::drafts::DraftChangeGroup& group = workspace->groups.back();
    EXPECT_EQ(group.source, core::drafts::DraftSource::SccgAiReview);
    EXPECT_EQ(group.source_label, "Claim review");
    EXPECT_EQ(group.source_session_id, "run-42");
    EXPECT_EQ(group.state, core::drafts::DraftGroupState::Ready);
    EXPECT_EQ(group.guideline_ids, std::vector<std::string>({"CL.1"}));
    EXPECT_EQ(group.review_item_ids, std::vector<std::string>({"ai-review-G1-claim_review-1"}));
    ASSERT_EQ(group.operations.size(), 1u);
    EXPECT_EQ(group.operations[0].old_value, "UNACCEPTED_WORKING_DRAFT_WORDING");
    EXPECT_EQ(group.operations[0].new_value, "AI_SUGGESTED_WORKING_DRAFT_WORDING");

    const std::optional<core::reviews::ReviewItem> linked =
        state.review_controller->GetItemById("ai-review-G1-claim_review-1");
    ASSERT_TRUE(linked.has_value());
    EXPECT_EQ(linked->draft_group_ids, std::vector<std::string>({group.id}));
    EXPECT_EQ(state.app_state.loaded_case->elements.front().content, "ACCEPTED_BASELINE_WORDING");

    const core::drafts::DraftMaterializationResult& after =
        state.draft_workspace.Materialize(*state.app_state.loaded_case, state.app_state.case_revision);
    ASSERT_TRUE(after.success) << after.error;
    ASSERT_NE(parser::FindElementByIdOrGidValue(after.working_model, "G1"), nullptr);
    EXPECT_EQ(parser::FindElementByIdOrGidValue(after.working_model, "G1")->content,
              "AI_SUGGESTED_WORKING_DRAFT_WORDING");
}

// The reason a scope hash exists. AI review and MCP write into the same draft,
// and so does the user's own hand editing, so on an actively edited case a
// whole-model hash discards nearly every completed review -- including reviews
// of branches nothing touched (ADR 0013).
TEST(AiReviewActionsTest, StagesSuggestionsWhenOnlyAnUnreviewedElementChanged) {
    TempDir temp = MakeTempDir("unrelated_edit");
    app::AppRuntimeState state;
    state.app_state.loaded_case = MakeAcceptedCaseWithASecondBranch();
    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    ASSERT_TRUE(state.review_controller->AddOrUpdateItem(MakeReviewItem()));

    const core::drafts::DraftMaterializationResult& before =
        state.draft_workspace.Materialize(*state.app_state.loaded_case, state.app_state.case_revision);
    ASSERT_TRUE(before.success) << before.error;

    app::AiReviewProposalSuggestionsEvent event;
    event.review_profile_name = "Claim review";
    event.review_run_id = "run-unrelated";
    event.reviewed_element_ids = {"G1"};
    event.reviewed_scope_hash = core::reviews::ComputeScopeSemanticHash(before.working_model, {"G1"});
    event.suggestions.push_back({"ai-review-G1-claim_review-1", "G1", "AI_SUGGESTED_WORDING"});

    // Somebody else edits a branch this review never read, while it is running.
    StageDraftEditTo(state, *state.app_state.loaded_case, "G2", "UNRELATED_BASELINE_WORDING", "SOMEONE_ELSES_EDIT");

    app::actions::ProposalActions(state).CreateAiGenerated(event);

    const std::optional<core::reviews::ReviewItem> linked =
        state.review_controller->GetItemById("ai-review-G1-claim_review-1");
    ASSERT_TRUE(linked.has_value());
    EXPECT_FALSE(linked->draft_group_ids.empty()) << "a review of an untouched branch is still valid";
}

// The other direction: an edit to an element the review actually read does
// invalidate it, because the suggestion answers text that no longer stands.
TEST(AiReviewActionsTest, RefusesSuggestionsWhenAReviewedElementChanged) {
    TempDir temp = MakeTempDir("in_scope_edit");
    app::AppRuntimeState state;
    state.app_state.loaded_case = MakeAcceptedCaseWithASecondBranch();
    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    ASSERT_TRUE(state.review_controller->AddOrUpdateItem(MakeReviewItem()));

    const core::drafts::DraftMaterializationResult& before =
        state.draft_workspace.Materialize(*state.app_state.loaded_case, state.app_state.case_revision);
    ASSERT_TRUE(before.success) << before.error;

    app::AiReviewProposalSuggestionsEvent event;
    event.review_profile_name = "Claim review";
    event.review_run_id = "run-in-scope";
    event.reviewed_element_ids = {"G1"};
    event.reviewed_scope_hash = core::reviews::ComputeScopeSemanticHash(before.working_model, {"G1"});
    event.suggestions.push_back({"ai-review-G1-claim_review-1", "G1", "AI_SUGGESTED_WORDING"});

    StageDraftEditTo(state, *state.app_state.loaded_case, "G1", "ACCEPTED_BASELINE_WORDING", "SOMEONE_ELSES_EDIT");

    app::actions::ProposalActions(state).CreateAiGenerated(event);

    const std::optional<core::reviews::ReviewItem> linked =
        state.review_controller->GetItemById("ai-review-G1-claim_review-1");
    ASSERT_TRUE(linked.has_value());
    EXPECT_TRUE(linked->draft_group_ids.empty()) << "the reviewed text changed under the suggestion";
}

TEST(AiReviewActionsTest, RefusesToStageSuggestionsWhenTheWorkingDraftChangedAfterReview) {
    TempDir temp = MakeTempDir("stale");
    app::AppRuntimeState state;
    state.app_state.loaded_case = MakeAcceptedCase();
    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    StageExistingDraftEdit(state, *state.app_state.loaded_case);
    ASSERT_TRUE(state.review_controller->AddOrUpdateItem(MakeReviewItem()));

    app::AiReviewProposalSuggestionsEvent event;
    event.review_profile_name = "Claim review";
    event.review_run_id = "run-stale";
    event.reviewed_element_ids = {"G1"};
    event.reviewed_scope_hash = "scope-hash-before-an-intervening-edit";
    event.suggestions.push_back({"ai-review-G1-claim_review-1", "G1", "STALE_SUGGESTION"});

    app::actions::ProposalActions(state).CreateAiGenerated(event);

    ASSERT_NE(state.draft_workspace.workspace(), nullptr);
    EXPECT_EQ(state.draft_workspace.workspace()->ActiveGroups().size(), 1u);
    EXPECT_TRUE(state.review_controller->GetItemById("ai-review-G1-claim_review-1")->draft_group_ids.empty());
}
