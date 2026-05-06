#include "ui/ui_state.h"

#include <gtest/gtest.h>

TEST(UiStateReviewVisualTest, RunningHasHighestInitialPriority) {
    ui::UiState state;

    ui::MarkReviewOkManually(state, "claim-1");
    ui::MarkAiReviewRunning(state, "claim-1", "profile-1", "Profile 1", {"claim-1", "claim-2"});

    const ui::ElementReviewVisualState* visual = ui::FindElementReviewVisualState(state, "claim-1");
    ASSERT_NE(visual, nullptr);
    EXPECT_EQ(ui::ResolveElementReviewVisualStatus(*visual), ui::ElementReviewVisualStatus::AiRunning);
    EXPECT_TRUE(visual->manual_ok);
    EXPECT_EQ(visual->review_profile_id, "profile-1");
    EXPECT_EQ(visual->review_profile_name, "Profile 1");
    EXPECT_EQ(state.ai_review_primary_element_id, "claim-1");
    EXPECT_TRUE(state.ai_review_scope_element_ids.count("claim-2") > 0);
}

TEST(UiStateReviewVisualTest, ManualOkBeatsAiOkAfterNoFindings) {
    ui::UiState state;

    ui::MarkReviewOkManually(state, "claim-1");
    ui::MarkAiReviewNoFindings(state, "claim-1");

    const ui::ElementReviewVisualState* visual = ui::FindElementReviewVisualState(state, "claim-1");
    ASSERT_NE(visual, nullptr);
    EXPECT_TRUE(visual->manual_ok);
    EXPECT_FALSE(visual->ai_ok);
    EXPECT_EQ(ui::ResolveElementReviewVisualStatus(*visual), ui::ElementReviewVisualStatus::ManualOk);
}

TEST(UiStateReviewVisualTest, NoFindingsShowsAiOkWhenNoManualOkExists) {
    ui::UiState state;

    ui::MarkAiReviewRunning(state, "claim-1");
    ui::MarkAiReviewNoFindings(state, "claim-1");

    const ui::ElementReviewVisualState* visual = ui::FindElementReviewVisualState(state, "claim-1");
    ASSERT_NE(visual, nullptr);
    EXPECT_FALSE(visual->ai_running);
    EXPECT_TRUE(visual->ai_ok);
    EXPECT_TRUE(state.ai_review_scope_element_ids.empty());
    EXPECT_TRUE(state.ai_review_primary_element_id.empty());
    EXPECT_EQ(ui::ResolveElementReviewVisualStatus(state, "claim-1"), ui::ElementReviewVisualStatus::AiOk);
}

TEST(UiStateReviewVisualTest, FailedClearsRunningAndAiOk) {
    ui::UiState state;

    ui::MarkAiReviewRunning(state, "claim-1");
    ui::MarkAiReviewNoFindings(state, "claim-1");
    ui::MarkAiReviewFailed(state, "claim-1", "AI review request failed.");

    const ui::ElementReviewVisualState* visual = ui::FindElementReviewVisualState(state, "claim-1");
    ASSERT_NE(visual, nullptr);
    EXPECT_FALSE(visual->ai_running);
    EXPECT_FALSE(visual->ai_ok);
    EXPECT_TRUE(visual->failed);
    EXPECT_EQ(ui::ResolveElementReviewVisualStatus(*visual), ui::ElementReviewVisualStatus::Failed);
}

TEST(UiStateReviewVisualTest, FindingsClearAiStatusButPreserveManualOk) {
    ui::UiState state;

    ui::MarkReviewOkManually(state, "claim-1");
    ui::MarkAiReviewRunning(state, "claim-1");
    ui::MarkAiReviewFindings(state, "claim-1");

    const ui::ElementReviewVisualState* visual = ui::FindElementReviewVisualState(state, "claim-1");
    ASSERT_NE(visual, nullptr);
    EXPECT_FALSE(visual->ai_running);
    EXPECT_FALSE(visual->ai_ok);
    EXPECT_FALSE(visual->failed);
    EXPECT_TRUE(visual->manual_ok);
    EXPECT_EQ(ui::ResolveElementReviewVisualStatus(*visual), ui::ElementReviewVisualStatus::ManualOk);
}

TEST(UiStateReviewVisualTest, EmptyAiFindingsStateIsRemoved) {
    ui::UiState state;

    ui::MarkAiReviewRunning(state, "claim-1");
    ui::MarkAiReviewFindings(state, "claim-1");

    EXPECT_EQ(ui::FindElementReviewVisualState(state, "claim-1"), nullptr);
    EXPECT_EQ(ui::ResolveElementReviewVisualStatus(state, "claim-1"), ui::ElementReviewVisualStatus::None);
}
