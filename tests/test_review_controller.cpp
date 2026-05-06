#include "app/controllers/review_controller.h"
#include "core/project_service.h"

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDir() noexcept {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::filesystem::path MakeTempDir() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();

    std::error_code ec;
    std::filesystem::path temp_root = std::filesystem::temp_directory_path(ec);
    if (ec) {
        ADD_FAILURE() << "Failed to obtain temporary directory path: " << ec.message();
        return {};
    }

    std::filesystem::path path = temp_root / ("assurance_forge_review_controller_test_" + std::to_string(stamp));

    std::filesystem::create_directories(path, ec);
    if (ec) {
        ADD_FAILURE() << "Failed to create temporary directory '" << path.string() << "': " << ec.message();
        return {};
    }

    if (!std::filesystem::is_directory(path)) {
        ADD_FAILURE() << "Temporary directory was not created: '" << path.string() << "'";
        return {};
    }

    return path;
}

struct ReviewHarness {
    app::AppEvents events;
    app::controllers::ReviewController controller;
    std::vector<std::string> statuses;
    int dirty_events = 0;

    ReviewHarness() : controller(events) {
        events.Subscribe<app::StatusMessageEvent>(
            [this](const app::StatusMessageEvent& event) { statuses.push_back(event.message); });
        events.Subscribe<app::ReviewItemsDirtyEvent>([this](const app::ReviewItemsDirtyEvent&) { ++dirty_events; });
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

} // namespace

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

TEST(ReviewControllerTest, ManualOkPersistsAndStatusRequiresNoOpenItems) {
    ReviewHarness harness;

    ASSERT_TRUE(harness.controller.SetManualReviewOk("claim-1", true, "Reviewer", "2026-05-06T12:00:00Z"));
    core::reviews::ElementReviewState state = harness.controller.ElementReviewStateForElement("claim-1");
    EXPECT_TRUE(state.manual_ok);
    EXPECT_EQ(state.reviewed_by, "Reviewer");
    EXPECT_EQ(harness.controller.StatusForElement("claim-1"), app::controllers::ElementReviewStatus::Passed);

    ASSERT_TRUE(harness.controller.AddOrUpdateItem(MakeReviewItem()));
    EXPECT_EQ(harness.controller.StatusForElement("claim-1"), app::controllers::ElementReviewStatus::OpenItems);
}

TEST(ReviewControllerTest, AiOutcomesDrivePersistedStatus) {
    ReviewHarness harness;

    ASSERT_TRUE(harness.controller.SetAiReviewOutcome("claim-1",
                                                      true,
                                                      false,
                                                      "profile-1",
                                                      "Profile 1",
                                                      "AI review completed with no findings.",
                                                      "2026-05-06T12:00:00Z"));
    core::reviews::ElementReviewState state = harness.controller.ElementReviewStateForElement("claim-1");
    EXPECT_TRUE(state.ai_ok);
    EXPECT_FALSE(state.failed);
    EXPECT_EQ(state.review_profile_id, "profile-1");
    EXPECT_EQ(harness.controller.StatusForElement("claim-1"), app::controllers::ElementReviewStatus::Passed);

    ASSERT_TRUE(harness.controller.SetAiReviewOutcome("claim-1",
                                                      false,
                                                      true,
                                                      "profile-1",
                                                      "Profile 1",
                                                      "AI review request failed.",
                                                      "2026-05-06T12:01:00Z"));
    state = harness.controller.ElementReviewStateForElement("claim-1");
    EXPECT_FALSE(state.ai_ok);
    EXPECT_TRUE(state.failed);
    EXPECT_EQ(state.last_review_message, "AI review request failed.");
    EXPECT_EQ(harness.controller.StatusForElement("claim-1"), app::controllers::ElementReviewStatus::Failed);
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
        [&](const std::string& proposal_id) { closed_preview = proposal_id == "proposal-1"; }));

    EXPECT_TRUE(deleted_linked_proposal);
    EXPECT_TRUE(closed_preview);
    EXPECT_FALSE(harness.controller.GetItemById(item.id).has_value());
    EXPECT_TRUE(harness.controller.IsDirty());
    EXPECT_EQ(harness.dirty_events, 1);
    EXPECT_EQ(harness.statuses.back(), "Deleted review comment and proposed change.");
}

TEST(ReviewControllerTest, SavingAndAddingAnotherCommentKeepsEarlierCommentOpen) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", temp.path, project, report, error)) << error;

    ReviewHarness harness;
    const std::filesystem::path review_path = project.rootPath / "reviews" / "review-items.af.json";
    ASSERT_TRUE(harness.controller.ConfigureStorage(review_path, error));

    core::reviews::ReviewItem first = MakeReviewItem("review-1");
    first.element_id = "claim-1";
    ASSERT_TRUE(harness.controller.AddManualItem(first));
    ASSERT_TRUE(harness.controller.SaveIfDirty(project, error)) << error;

    ASSERT_TRUE(harness.controller.ConfigureStorage(review_path, error)) << error;

    core::reviews::ReviewItem second = MakeReviewItem("review-2");
    second.element_id = "claim-2";
    ASSERT_TRUE(harness.controller.AddManualItem(second));

    std::optional<core::reviews::ReviewItem> reloaded_first = harness.controller.GetItemById("review-1");
    ASSERT_TRUE(reloaded_first.has_value());
    EXPECT_EQ(reloaded_first->status, core::reviews::ReviewItemStatus::Open);

    std::optional<core::reviews::ReviewItem> reloaded_second = harness.controller.GetItemById("review-2");
    ASSERT_TRUE(reloaded_second.has_value());
    EXPECT_EQ(reloaded_second->status, core::reviews::ReviewItemStatus::Open);
}

TEST(ReviewControllerTest, ConfigureStorageSamePathDoesNotReloadAndClobberInMemoryState) {
    TempDir temp(MakeTempDir());
    core::AssuranceProject project;
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("MySafetyCase", temp.path, project, report, error)) << error;

    ReviewHarness harness;
    const std::filesystem::path review_path = project.rootPath / "reviews" / "review-items.af.json";
    ASSERT_TRUE(harness.controller.ConfigureStorage(review_path, error)) << error;

    core::reviews::ReviewItem item = MakeReviewItem("review-1");
    item.status = core::reviews::ReviewItemStatus::Resolved;
    ASSERT_TRUE(harness.controller.AddOrUpdateItem(item));
    ASSERT_TRUE(harness.controller.SaveIfDirty(project, error)) << error;

    ASSERT_TRUE(harness.controller.ConfigureStorage(review_path, error)) << error;

    core::reviews::ReviewItem reopened = item;
    reopened.status = core::reviews::ReviewItemStatus::Open;
    reopened.updated_utc = "2026-05-01T02:00:00Z";
    ASSERT_TRUE(harness.controller.AddOrUpdateItem(reopened));

    ASSERT_TRUE(harness.controller.ConfigureStorage(review_path, error)) << error;

    std::optional<core::reviews::ReviewItem> found = harness.controller.GetItemById("review-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, core::reviews::ReviewItemStatus::Open);
    EXPECT_EQ(found->updated_utc, "2026-05-01T02:00:00Z");
}
