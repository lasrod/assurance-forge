#include "ui/ui_state.h"

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
