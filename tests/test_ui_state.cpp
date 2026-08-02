#include "ui/ui_state.h"

#include "core/reviews/review_item.h"

#include <gtest/gtest.h>

TEST(UiStateAiSpinnerTest, BeginAddsRunningElementAndScope) {
    ui::UiState state;

    ui::BeginAiReviewSpinner(state, "claim-1", {"claim-1", "claim-2", "claim-3"});

    EXPECT_TRUE(state.ai_review_running_element_ids.count("claim-1") > 0);
    EXPECT_EQ(state.ai_review_primary_element_id, "claim-1");
    EXPECT_EQ(state.ai_review_scope_element_ids.size(), 3u);
    EXPECT_TRUE(state.ai_review_scope_element_ids.count("claim-2") > 0);
}

TEST(UiStateAiSpinnerTest, EndRemovesPrimaryElementAndClearsScope) {
    ui::UiState state;

    ui::BeginAiReviewSpinner(state, "claim-1", {"claim-1", "claim-2"});
    ui::EndAiReviewSpinner(state, "claim-1");

    EXPECT_TRUE(state.ai_review_running_element_ids.empty());
    EXPECT_TRUE(state.ai_review_primary_element_id.empty());
    EXPECT_TRUE(state.ai_review_scope_element_ids.empty());
}

TEST(UiStateAiSpinnerTest, EndSecondaryElementKeepsPrimaryScope) {
    ui::UiState state;

    ui::BeginAiReviewSpinner(state, "claim-1", {"claim-1", "claim-2"});
    ui::BeginAiReviewSpinner(state, "claim-2", {});
    ui::EndAiReviewSpinner(state, "claim-2");

    EXPECT_TRUE(state.ai_review_running_element_ids.count("claim-1") > 0);
    EXPECT_FALSE(state.ai_review_running_element_ids.count("claim-2") > 0);
    EXPECT_EQ(state.ai_review_primary_element_id, "claim-1");
    EXPECT_TRUE(state.ai_review_scope_element_ids.count("claim-1") > 0);
    EXPECT_TRUE(state.ai_review_scope_element_ids.count("claim-2") > 0);
}

TEST(UiStateAiSpinnerTest, BeginWithoutScopeDoesNotOverridePrimary) {
    ui::UiState state;

    ui::BeginAiReviewSpinner(state, "claim-1", {"claim-1"});
    ui::BeginAiReviewSpinner(state, "claim-2", {});

    EXPECT_EQ(state.ai_review_primary_element_id, "claim-1");
    EXPECT_TRUE(state.ai_review_running_element_ids.count("claim-2") > 0);
}

TEST(UiStateAiSpinnerTest, BeginWithScopeDoesNotOverridePrimary) {
    ui::UiState state;

    ui::BeginAiReviewSpinner(state, "claim-1", {"claim-1", "claim-2"});
    ui::BeginAiReviewSpinner(state, "claim-3", {"claim-3", "claim-4"});

    EXPECT_EQ(state.ai_review_primary_element_id, "claim-1");
    EXPECT_TRUE(state.ai_review_running_element_ids.count("claim-3") > 0);
    EXPECT_TRUE(state.ai_review_scope_element_ids.count("claim-1") > 0);
    EXPECT_TRUE(state.ai_review_scope_element_ids.count("claim-2") > 0);
    EXPECT_FALSE(state.ai_review_scope_element_ids.count("claim-3") > 0);
    EXPECT_FALSE(state.ai_review_scope_element_ids.count("claim-4") > 0);
}

TEST(UiStateFocusProblemTest, SetsFocusFlagsAndProblemSelection) {
    ui::UiState state;

    ui::FocusProblemInPanel(state, "problem-42", "claim-1");

    EXPECT_EQ(state.selected_problem_id, "problem-42");
    EXPECT_EQ(state.selected_problem_element_id, "claim-1");
    EXPECT_TRUE(state.problems_panel_open_pending);
    EXPECT_TRUE(state.problems_panel_focus_pending);
}

TEST(UiStateFocusProblemTest, EmptyProblemIdClearsPendingFlags) {
    ui::UiState state;
    state.problems_panel_focus_pending = true;
    state.problems_panel_open_pending = true;

    ui::FocusProblemInPanel(state, "", "");

    EXPECT_TRUE(state.selected_problem_id.empty());
    EXPECT_TRUE(state.selected_problem_element_id.empty());
    EXPECT_FALSE(state.problems_panel_focus_pending);
    EXPECT_FALSE(state.problems_panel_open_pending);
}

TEST(UiStateAiReviewOutcomeTest, SuccessfulReviewsProducePersistentCanvasMarkers) {
    ui::UiState state;
    core::reviews::ElementReviewStateMap outcomes;
    outcomes["G1"].ai_ok = true;
    outcomes["G1"].review_profile_name = "Claim review";
    outcomes["G1"].last_review_message = "AI review completed with no findings.";
    outcomes["G2"].failed = true;

    ui::SyncAiReviewSuccessMarkers(state, outcomes);

    ASSERT_EQ(state.ai_review_success_markers.size(), 1u);
    EXPECT_EQ(state.ai_review_success_markers.at("G1").review_profile_name, "Claim review");
    EXPECT_EQ(state.ai_review_success_markers.at("G1").message, "AI review completed with no findings.");
    EXPECT_EQ(state.ai_review_success_markers.count("G2"), 0u);

    outcomes["G1"].ai_ok = false;
    ui::SyncAiReviewSuccessMarkers(state, outcomes);
    EXPECT_TRUE(state.ai_review_success_markers.empty());
}
