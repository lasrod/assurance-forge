#include "app/actions/ai_review_actions.h"
#include "app/actions/proposal_actions.h"
#include "app/app_runtime_state.h"
#include "app/commands/dispatch.h"
#include "core/drafts/draft_provenance.h"
#include "core/guideline_catalog.h"
#include "core/project_file_io.h"
#include "core/reviews/review_proposal.h"
#include "parser/model_utils.h"
#include "review/sccg/suggestion_mapping.h"
#include "sacm_adapter/library_load.h"
#include "ui/ui_state.h"

#include <gtest/gtest.h>

#include <chrono>
#include <expected>
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

// S5's exit criterion, end to end against the real draft workspace: an AR.2
// finding produces a draft group that ADDS a strategy and hangs the sub-claim
// beneath it. No amount of rewording the goal expresses this repair, which is
// what the review could do before.
//
// Staging through the workspace is the point of testing it here rather than in
// the mapper's own tests: the draft document refuses an operation the SACM model
// cannot hold, in the call that makes it (ADR 0016).
TEST(AiReviewActionsTest, AnAr2FindingStagesACreatedStrategyAndItsAttachments) {
    TempDir temp = MakeTempDir("structural");
    app::AppRuntimeState state;
    state.app_state.loaded_case = MakeAcceptedCaseWithASecondBranch();
    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    ASSERT_TRUE(state.review_controller->AddOrUpdateItem(MakeReviewItem()));

    const core::drafts::DraftMaterializationResult& before =
        state.draft_workspace.Materialize(*state.app_state.loaded_case, state.app_state.case_revision);
    ASSERT_TRUE(before.success) << before.error;

    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateStrategy;
    create.create_ref = "$strategy";
    create.text = "Argue over the credible hazard classes";

    core::reviews::PatchOperation attach;
    attach.type = core::reviews::PatchOperationType::AddSupportedBy;
    attach.source = core::reviews::ElementRef{std::nullopt, "$strategy"};
    attach.target = core::reviews::ElementRef{"G1", std::nullopt};

    app::AiReviewProposalSuggestionsEvent event;
    event.review_profile_name = "Claim review";
    event.review_run_id = "run-ar2";
    event.reviewed_element_ids = {"G1", "G2"};
    event.reviewed_scope_hash = core::reviews::ComputeScopeSemanticHash(before.working_model, {"G1", "G2"});
    app::AiReviewProposalSuggestion suggestion;
    suggestion.review_item_id = "ai-review-G1-claim_review-1";
    suggestion.element_id = "G1";
    suggestion.proposed_operations = {create, attach};
    event.suggestions.push_back(std::move(suggestion));

    app::actions::ProposalActions(state).CreateAiGenerated(event);

    const core::drafts::DraftWorkspace* workspace = state.draft_workspace.workspace();
    ASSERT_NE(workspace, nullptr);
    ASSERT_EQ(workspace->ActiveGroups().size(), 1u);
    const core::drafts::DraftChangeGroup& group = workspace->groups.back();
    EXPECT_EQ(group.source, core::drafts::DraftSource::SccgAiReview);
    EXPECT_EQ(group.state, core::drafts::DraftGroupState::Ready);
    EXPECT_EQ(group.operations.size(), 2u);

    // The strategy is really in the working argument, not just in the ledger.
    const core::drafts::DraftMaterializationResult& after =
        state.draft_workspace.Materialize(*state.app_state.loaded_case, state.app_state.case_revision);
    ASSERT_TRUE(after.success) << after.error;
    bool strategy_present = false;
    for (const parser::SacmElement& element : after.working_model.elements) {
        if (element.content == "Argue over the credible hazard classes")
            strategy_present = true;
    }
    EXPECT_TRUE(strategy_present) << "the created strategy never reached the working draft";

    // Accepted SACM is untouched until a human promotes it.
    EXPECT_EQ(state.app_state.loaded_case->elements.size(), 2u);
}

// The scope rule reaches the application surface, not only the mapper: an
// operation naming an element this review never read stages nothing.
TEST(AiReviewActionsTest, RefusesAStructuralRepairReachingOutsideTheReviewedScope) {
    TempDir temp = MakeTempDir("out_of_scope");
    app::AppRuntimeState state;
    state.app_state.loaded_case = MakeAcceptedCaseWithASecondBranch();
    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    ASSERT_TRUE(state.review_controller->AddOrUpdateItem(MakeReviewItem()));

    const core::drafts::DraftMaterializationResult& before =
        state.draft_workspace.Materialize(*state.app_state.loaded_case, state.app_state.case_revision);
    ASSERT_TRUE(before.success) << before.error;

    core::reviews::PatchOperation reach;
    reach.type = core::reviews::PatchOperationType::AddSupportedBy;
    reach.source = core::reviews::ElementRef{"G2", std::nullopt};
    reach.target = core::reviews::ElementRef{"G1", std::nullopt};

    app::AiReviewProposalSuggestionsEvent event;
    event.review_profile_name = "Claim review";
    event.review_run_id = "run-reach";
    // The review read only G1; G2 is a branch it never saw.
    event.reviewed_element_ids = {"G1"};
    event.reviewed_scope_hash = core::reviews::ComputeScopeSemanticHash(before.working_model, {"G1"});
    app::AiReviewProposalSuggestion suggestion;
    suggestion.review_item_id = "ai-review-G1-claim_review-1";
    suggestion.element_id = "G1";
    suggestion.proposed_operations = {reach};
    event.suggestions.push_back(std::move(suggestion));

    app::actions::ProposalActions(state).CreateAiGenerated(event);

    // A draft is created by the first unaccepted change, so refusing every
    // operation leaves no workspace at all -- which is the strongest form of
    // "nothing was staged" this surface can show.
    const core::drafts::DraftWorkspace* workspace = state.draft_workspace.workspace();
    EXPECT_TRUE(workspace == nullptr || workspace->ActiveGroups().empty());
    EXPECT_TRUE(state.review_controller->GetItemById("ai-review-G1-claim_review-1")->draft_group_ids.empty());
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

// With a draft DOCUMENT active (ADR 0016), the document is the working argument:
// it is what the canvas draws and what Accept writes. A suggestion staged
// anywhere else is shown to nobody and accepted by nothing.
TEST(AiReviewActionsTest, WithADraftDocumentASuggestionReachesTheDocumentAndSurvivesAccept) {
    TempDir temp = MakeTempDir("document_backed");
    const std::filesystem::path argument = temp.path / "argument.sacm";
    const sacm_adapter::SaveOutcome seed = sacm_adapter::new_case_document_xmi("Kettle");
    ASSERT_TRUE(seed.ok);
    ASSERT_TRUE(core::WriteTextFileAtomic(argument, seed.xml).has_value());

    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(argument.string())) << state.app_state.status_message;
    ASSERT_NE(state.app_state.library_document, nullptr);
    std::string claim_id;
    for (const parser::SacmElement& element : state.app_state.loaded_case->elements) {
        if (element.type == "claim") {
            claim_id = element.id;
            break;
        }
    }
    ASSERT_FALSE(claim_id.empty());

    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    std::string error;
    ASSERT_TRUE(state.draft_document.Open(temp.path, argument, *state.app_state.library_document, error)) << error;
    ASSERT_TRUE(state.draft_document.EnsureDraft(*state.app_state.library_document, error)) << error;

    core::reviews::ReviewItem item = MakeReviewItem();
    item.element_id = claim_id;
    ASSERT_TRUE(state.review_controller->AddOrUpdateItem(item));

    const core::drafts::DraftMaterializationResult& before =
        state.draft_workspace.Materialize(*state.app_state.loaded_case, state.app_state.case_revision);
    ASSERT_TRUE(before.success) << before.error;

    app::AiReviewProposalSuggestionsEvent event;
    event.review_profile_id = "claim_review";
    event.review_profile_name = "Claim review";
    event.review_run_id = "run-42";
    event.reviewed_element_ids = {claim_id};
    event.reviewed_scope_hash = core::reviews::ComputeScopeSemanticHash(before.working_model, {claim_id});
    event.suggestions.push_back({item.id, claim_id, "AI_SUGGESTED_DOCUMENT_WORDING"});

    app::actions::ProposalActions(state).CreateAiGenerated(event);

    // Staged, not refused: the review's group exists and is linked to its
    // finding. What is under test is where the change went.
    const core::drafts::DraftWorkspace* workspace = state.draft_workspace.workspace();
    ASSERT_NE(workspace, nullptr);
    ASSERT_EQ(workspace->ActiveGroups().size(), 1u);
    EXPECT_EQ(workspace->groups.back().source, core::drafts::DraftSource::SccgAiReview);
    const std::optional<core::reviews::ReviewItem> linked = state.review_controller->GetItemById(item.id);
    ASSERT_TRUE(linked.has_value());
    EXPECT_EQ(linked->draft_group_ids.size(), 1u);

    const parser::AssuranceCase draft = state.draft_document.Projection();
    const parser::SacmElement* suggested = parser::FindElementByIdOrGidValue(draft, claim_id);
    ASSERT_NE(suggested, nullptr);
    // Whichever field the review read -- the seeded claim has no content yet.
    EXPECT_EQ(review::TextTargetFor(*suggested).current_text, "AI_SUGGESTED_DOCUMENT_WORDING")
        << "the suggestion must be in the draft the user sees and accepts";

    const std::vector<core::drafts::DraftContribution> contributions =
        core::drafts::ReadDraftProvenance(*state.draft_document.document());
    ASSERT_EQ(contributions.size(), 1u);
    EXPECT_EQ(contributions.front().provenance.contribution_id, workspace->groups.back().id);
    EXPECT_EQ(contributions.front().provenance.source, core::drafts::DraftSource::SccgAiReview);
    EXPECT_EQ(contributions.front().provenance.label, "Claim review");
    EXPECT_EQ(contributions.front().provenance.session_id, "run-42");

    state.draft_document.MarkChanged();
    ASSERT_TRUE(state.draft_document.AcceptInto(argument, error)) << error;
    const std::expected<std::string, std::string> written = core::ReadTextFile(argument);
    ASSERT_TRUE(written.has_value());
    EXPECT_NE(written->find("AI_SUGGESTED_DOCUMENT_WORDING"), std::string::npos)
        << "accepting the draft must accept the suggestion it was shown with";
}

// The review reads the draft document when there is one: that is the argument
// the user sees. A hand edit lives only in the document -- no change group
// records it -- so a review reading the change-group materialization judged the
// accepted wording the user had already replaced.
TEST(AiReviewActionsTest, WithADraftDocumentTheReviewReadsTheUsersDraftEdits) {
    TempDir temp = MakeTempDir("document_review_input");
    const std::filesystem::path argument = temp.path / "argument.sacm";
    const sacm_adapter::SaveOutcome seed = sacm_adapter::new_case_document_xmi("Kettle");
    ASSERT_TRUE(seed.ok);
    ASSERT_TRUE(core::WriteTextFileAtomic(argument, seed.xml).has_value());

    app::AppRuntimeState state;
    ASSERT_TRUE(state.app_state.load_file(argument.string())) << state.app_state.status_message;
    ASSERT_NE(state.app_state.library_document, nullptr);
    std::string claim_id;
    for (const parser::SacmElement& element : state.app_state.loaded_case->elements) {
        if (element.type == "claim") {
            claim_id = element.id;
            break;
        }
    }
    ASSERT_FALSE(claim_id.empty());

    OpenDraftStore(state, temp, *state.app_state.loaded_case);
    std::string error;
    ASSERT_TRUE(state.draft_document.Open(temp.path, argument, *state.app_state.library_document, error)) << error;
    ASSERT_TRUE(state.draft_document.EnsureDraft(*state.app_state.library_document, error)) << error;

    const parser::SacmElement* accepted_claim =
        parser::FindElementByIdOrGidValue(*state.app_state.loaded_case, claim_id);
    ASSERT_NE(accepted_claim, nullptr);
    core::reviews::PatchOperation reword;
    reword.type = core::reviews::PatchOperationType::UpdateElementText;
    reword.element = core::reviews::ElementRef{claim_id, std::nullopt};
    reword.field = review::TextTargetFor(*accepted_claim).field;
    reword.new_value = "HAND_EDITED_DRAFT_WORDING";
    const app::commands::DraftEditOutcome edited = app::commands::DispatchDraftDocumentEdit(state, {reword});
    ASSERT_TRUE(edited.success) << edited.error;

    state.guideline_catalog = MakeClaimReviewCatalog();
    ui::GetUiState().selected_element_id = claim_id;
    app::actions::AiReviewActions(state).BeginForSelection();

    ASSERT_TRUE(state.ai.review_controller->HasPendingRequest());
    EXPECT_NE(state.ai.review_controller->PendingPrompt().find("HAND_EDITED_DRAFT_WORDING"), std::string::npos)
        << "the review must read the wording the user sees in the draft";
    EXPECT_NE(state.ai.review_controller->PendingDebugText().find("unaccepted working-draft content"),
              std::string::npos);
    ui::GetUiState().selected_element_id.clear();
}
