#include "core/reviews/review_item.h"
#include "core/reviews/review_item_manager.h"

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>

#include <fstream>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path p) : path(std::move(p)) {}
    ~TempDir() {
        std::filesystem::remove_all(path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::filesystem::path MakeTempDir() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("assurance_forge_review_item_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

core::reviews::ReviewItem MakeItem(const std::string& id, const std::string& element_id) {
    core::reviews::ReviewItem item;
    item.id = id;
    item.element_id = element_id;
    item.title = "Review comment";
    item.message = "Example comment.";
    item.severity = "warning";
    item.created_utc = "2026-04-30T12:00:00Z";
    item.updated_utc = item.created_utc;
    return item;
}

} // namespace

TEST(ReviewItemTest, RoundTripsReviewItemsJson) {
    core::reviews::ReviewItem item;
    item.id = "review-0017";
    item.element_id = "G12";
    item.title = "Split mixed claim";
    item.message = "Claim combines safety and cybersecurity.";
    item.severity = "warning";
    item.reviewer_name = "Case Reviewer";
    item.guideline_ids = {"CL.1", "AR.2"};
    item.source = core::reviews::ReviewItemSource::AIReview;
    item.status = core::reviews::ReviewItemStatus::Resolved;
    item.proposal_id = "proposal-0001";
    item.applied_note = "Applied proposal proposal-0001.";
    item.created_utc = "2026-04-30T12:00:00Z";
    item.updated_utc = "2026-04-30T12:05:00Z";

    std::string error;
    std::vector<core::reviews::ReviewItem> items;
    ASSERT_TRUE(core::reviews::DeserializeReviewItems(core::reviews::SerializeReviewItems({item}), items, error))
        << error;

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].id, item.id);
    EXPECT_EQ(items[0].proposal_id, item.proposal_id);
    EXPECT_EQ(items[0].reviewer_name, item.reviewer_name);
    EXPECT_EQ(items[0].guideline_ids, item.guideline_ids);
    EXPECT_EQ(items[0].source, core::reviews::ReviewItemSource::AIReview);
    EXPECT_EQ(items[0].status, core::reviews::ReviewItemStatus::Resolved);
}

TEST(ReviewItemTest, RoundTripsElementReviewStatesJson) {
    core::reviews::ReviewItem item = MakeItem("review-1", "G1");
    core::reviews::ElementReviewStateMap states;
    states["G1"].manual_ok = true;
    states["G1"].ai_ok = true;
    states["G1"].review_profile_id = "claim_wording_review";
    states["G1"].review_profile_name = "Claim wording review";
    states["G1"].last_review_message = "AI review completed with no findings.";
    states["G1"].reviewed_by = "Reviewer";
    states["G1"].updated_utc = "2026-05-06T12:00:00Z";

    std::string error;
    std::vector<core::reviews::ReviewItem> items;
    core::reviews::ElementReviewStateMap loaded_states;
    ASSERT_TRUE(core::reviews::DeserializeReviewItems(
        core::reviews::SerializeReviewItems({item}, states), items, loaded_states, error))
        << error;

    ASSERT_EQ(items.size(), 1u);
    ASSERT_EQ(loaded_states.size(), 1u);
    EXPECT_TRUE(loaded_states["G1"].manual_ok);
    EXPECT_TRUE(loaded_states["G1"].ai_ok);
    EXPECT_FALSE(loaded_states["G1"].failed);
    EXPECT_EQ(loaded_states["G1"].review_profile_id, "claim_wording_review");
    EXPECT_EQ(loaded_states["G1"].review_profile_name, "Claim wording review");
    EXPECT_EQ(loaded_states["G1"].last_review_message, "AI review completed with no findings.");
    EXPECT_EQ(loaded_states["G1"].reviewed_by, "Reviewer");
    EXPECT_EQ(loaded_states["G1"].updated_utc, "2026-05-06T12:00:00Z");
}

TEST(ReviewItemTest, DeserializesOldReviewItemsWithoutGuidelineIds) {
    const std::string content = R"json({
    "format": "assurance-forge-review-items",
    "formatVersion": "0.1.0",
    "items": [
        {
            "id": "review-legacy",
            "element_id": "G1",
            "title": "Legacy comment",
            "message": "Created before guideline tags existed.",
            "severity": "warning",
            "reviewer_name": "Reviewer",
            "source": "manual",
            "status": "open",
            "applied_note": "",
            "created_utc": "2026-04-30T12:00:00Z",
            "updated_utc": "2026-04-30T12:00:00Z"
        }
    ]
})json";

    std::string error;
    std::vector<core::reviews::ReviewItem> items;
    core::reviews::ElementReviewStateMap states;
    ASSERT_TRUE(core::reviews::DeserializeReviewItems(content, items, states, error)) << error;

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].id, "review-legacy");
    EXPECT_TRUE(items[0].guideline_ids.empty());
    EXPECT_TRUE(states.empty());
}

TEST(ReviewItemTest, RejectsUnsupportedReviewItemFormat) {
    std::string error;
    std::vector<core::reviews::ReviewItem> items;

    EXPECT_FALSE(core::reviews::DeserializeReviewItems("{\"format\":\"other\",\"items\":[]}", items, error));
    EXPECT_FALSE(error.empty());
}

TEST(ReviewItemManagerTest, SavesLoadsAndFiltersItemsByElement) {
    TempDir temp(MakeTempDir());
    std::filesystem::path review_path = temp.path / "reviews" / "review-items.af.json";

    core::reviews::ReviewItemManager manager;
    manager.SetFilePath(review_path);
    ASSERT_TRUE(manager.AddOrUpdateItem(MakeItem("review-1", "G1")));
    ASSERT_TRUE(manager.AddOrUpdateItem(MakeItem("review-2", "G2")));

    std::string error;
    ASSERT_TRUE(manager.Save(error)) << error;

    core::reviews::ReviewItemManager loaded;
    loaded.SetFilePath(review_path);
    ASSERT_TRUE(loaded.Load(error)) << error;

    EXPECT_EQ(loaded.GetItems().size(), 2u);
    std::vector<core::reviews::ReviewItem> g1_items = loaded.GetItemsForElement("G1");
    ASSERT_EQ(g1_items.size(), 1u);
    EXPECT_EQ(g1_items[0].id, "review-1");
}

TEST(ReviewItemManagerTest, SavesLoadsElementReviewStates) {
    TempDir temp(MakeTempDir());
    std::filesystem::path review_path = temp.path / "reviews" / "review-items.af.json";

    core::reviews::ReviewItemManager manager;
    manager.SetFilePath(review_path);
    ASSERT_TRUE(manager.AddOrUpdateItem(MakeItem("review-1", "G1")));
    core::reviews::ElementReviewState state;
    state.manual_ok = true;
    state.ai_ok = true;
    state.reviewed_by = "Reviewer";
    ASSERT_TRUE(manager.SetElementReviewState("G1", state));

    std::string error;
    ASSERT_TRUE(manager.Save(error)) << error;

    core::reviews::ReviewItemManager loaded;
    loaded.SetFilePath(review_path);
    ASSERT_TRUE(loaded.Load(error)) << error;

    EXPECT_TRUE(loaded.GetElementReviewState("G1").manual_ok);
    EXPECT_TRUE(loaded.GetElementReviewState("G1").ai_ok);
    EXPECT_EQ(loaded.GetElementReviewState("G1").reviewed_by, "Reviewer");
}

// A failed load keeps what it had.
//
// The review file has a second writer -- another Assurance Forge process, or a
// text editor -- and the controller polls it for external changes, so a read
// that lands mid-flush parses as garbage. `Load` cleared its contents before it
// knew whether it could replace them, so the user's review comments disappeared
// from the panel because of a read, and stayed gone until the next poll that
// happened to succeed. Nothing on disk had changed.
TEST(ReviewItemManagerTest, KeepsWhatItHasWhenALoadFails) {
    TempDir temp(MakeTempDir());
    const std::filesystem::path review_path = temp.path / "reviews" / "review-items.af.json";

    core::reviews::ReviewItemManager manager;
    manager.SetFilePath(review_path);
    ASSERT_TRUE(manager.AddOrUpdateItem(MakeItem("review-1", "G1")));
    std::string error;
    ASSERT_TRUE(manager.Save(error)) << error;
    ASSERT_EQ(manager.GetItems().size(), 1u);

    // Half a flush: valid JSON up to the point the writer got to.
    std::ofstream(review_path, std::ios::trunc) << R"({"format":"assurance-forge-rev)";

    EXPECT_FALSE(manager.Load(error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(manager.GetItems().size(), 1u) << "a read that failed emptied the review items";
    EXPECT_EQ(manager.GetItemsForElement("G1").size(), 1u);
}

TEST(ReviewItemManagerTest, UpdatesAndRemovesItems) {
    core::reviews::ReviewItemManager manager;
    ASSERT_TRUE(manager.AddOrUpdateItem(MakeItem("review-1", "G1")));

    core::reviews::ReviewItem updated = MakeItem("review-1", "G2");
    updated.proposal_id = "proposal-0001";
    ASSERT_TRUE(manager.AddOrUpdateItem(updated));

    std::optional<core::reviews::ReviewItem> found = manager.GetItemById("review-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->element_id, "G2");
    EXPECT_EQ(found->proposal_id, "proposal-0001");

    ASSERT_TRUE(manager.ClearProposal("review-1"));
    found = manager.GetItemById("review-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->proposal_id.has_value());

    ASSERT_TRUE(manager.RemoveItem("review-1"));
    EXPECT_FALSE(manager.GetItemById("review-1").has_value());
}