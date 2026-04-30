#include <gtest/gtest.h>

#include "app/review_controller.h"

#include <string>
#include <vector>

namespace {

struct ReviewHarness {
    app::AppEvents events;
    app::ReviewController controller;
    std::vector<std::string> statuses;
    int dirty_events = 0;

    ReviewHarness() : controller(events) {
        events.Subscribe<app::StatusMessageEvent>([this](const app::StatusMessageEvent& event) {
            statuses.push_back(event.message);
        });
        events.Subscribe<app::ReviewItemsDirtyEvent>([this](const app::ReviewItemsDirtyEvent&) {
            ++dirty_events;
        });
    }
};

core::reviews::ReviewItem MakeReviewItem(std::string id = "review-1") {
    core::reviews::ReviewItem item;
    item.id = std::move(id);
    item.element_id = "claim-1";
    item.title = "Finding";
    item.message = "Message";
    item.severity = "warning";
    item.reviewer_name = "Reviewer";
    item.source = core::reviews::ReviewItemSource::Manual;
    item.status = core::reviews::ReviewItemStatus::Open;
    item.created_utc = "2026-05-01T00:00:00Z";
    item.updated_utc = item.created_utc;
    return item;
}

}  // namespace

TEST(ReviewControllerTest, AddManualItemStoresItemMarksDirtyAndEmitsStatus) {
    ReviewHarness harness;

    ASSERT_TRUE(harness.controller.AddManualItem(MakeReviewItem()));

    EXPECT_TRUE(harness.controller.IsDirty());
    EXPECT_EQ(harness.dirty_events, 1);
    ASSERT_EQ(harness.controller.ItemsForElement("claim-1").size(), 1u);
    ASSERT_FALSE(harness.statuses.empty());
    EXPECT_EQ(harness.statuses.back(), "Review comment added.");
}

TEST(ReviewControllerTest, ResolveReviewItemUpdatesStatusAndMarksDirty) {
    ReviewHarness harness;
    core::reviews::ReviewItem item = MakeReviewItem();
    ASSERT_TRUE(harness.controller.AddOrUpdateItem(item));
    harness.controller.ClearDirty();
    harness.dirty_events = 0;

    ASSERT_TRUE(harness.controller.ResolveReviewItem(item, false, true, "2026-05-01T01:00:00Z"));

    std::optional<core::reviews::ReviewItem> updated = harness.controller.GetItemById(item.id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->status, core::reviews::ReviewItemStatus::Resolved);
    EXPECT_EQ(updated->updated_utc, "2026-05-01T01:00:00Z");
    EXPECT_TRUE(harness.controller.IsDirty());
    EXPECT_EQ(harness.dirty_events, 1);
    EXPECT_EQ(harness.statuses.back(), "Review comment resolved.");
}

TEST(ReviewControllerTest, BeginDeleteReviewItemWithProposalRequestsConfirmation) {
    ReviewHarness harness;
    core::reviews::ReviewItem item = MakeReviewItem();
    item.proposal_id = "proposal-1";

    harness.controller.BeginDeleteReviewItem(item, false);

    EXPECT_TRUE(harness.controller.ShouldShowDeleteConfirm());
    EXPECT_EQ(harness.controller.PendingDeleteReviewItem().id, item.id);

    harness.controller.CancelDeleteReviewItem();

    EXPECT_FALSE(harness.controller.ShouldShowDeleteConfirm());
}

TEST(ReviewControllerTest, DeleteReviewItemDeletesLinkedProposalAndItem) {
    ReviewHarness harness;
    core::reviews::ReviewItem item = MakeReviewItem();
    item.proposal_id = "proposal-1";
    ASSERT_TRUE(harness.controller.AddOrUpdateItem(item));
    harness.controller.ClearDirty();
    harness.dirty_events = 0;

    bool deleted_linked_proposal = false;
    bool closed_preview = false;
    ASSERT_TRUE(harness.controller.DeleteReviewItem(
        item,
        false,
        true,
        [&](const std::string& proposal_id, std::string&) {
            deleted_linked_proposal = proposal_id == "proposal-1";
            return true;
        },
        [&](const std::string& proposal_id) {
            closed_preview = proposal_id == "proposal-1";
        }));

    EXPECT_TRUE(deleted_linked_proposal);
    EXPECT_TRUE(closed_preview);
    EXPECT_FALSE(harness.controller.GetItemById(item.id).has_value());
    EXPECT_TRUE(harness.controller.IsDirty());
    EXPECT_EQ(harness.dirty_events, 1);
    EXPECT_EQ(harness.statuses.back(), "Deleted review comment and proposed change.");
}
