#include "app/controllers/review_controller.h"

#include "app/app_events.h"
#include "core/reviews/review_item.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

// Review items are written by more than one process. The MCP server saves a
// proposal and appends the review item that makes it visible; the running app
// had read that file once, on project open, so the comment stayed invisible
// until the project was reopened. Reported from real use: "I do still not see
// the proposal unless I restart Assurance Forge."

namespace {

std::filesystem::path ScratchFile(const std::string& stem) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "af_review_external_reload";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / (stem + ".af.json");
    std::filesystem::remove(path);
    return path;
}

core::reviews::ReviewItem MakeItem(const std::string& id, const std::string& title) {
    core::reviews::ReviewItem item;
    item.id = id;
    item.element_id = "G1";
    item.title = title;
    item.message = "written by another process";
    item.severity = "info";
    item.reviewer_name = "MCP: test-client 1.0";
    item.source = core::reviews::ReviewItemSource::AIReview;
    item.status = core::reviews::ReviewItemStatus::Open;
    item.created_utc = "2026-07-27T12:00:00Z";
    item.updated_utc = item.created_utc;
    return item;
}

// Writes the file the way another process would: full contents, from outside.
void WriteItemsExternally(const std::filesystem::path& path, const std::vector<core::reviews::ReviewItem>& items) {
    std::ofstream(path, std::ios::trunc) << core::reviews::SerializeReviewItems(items);
}

// The poll is throttled, so a test that changes a file and immediately asks must
// wait past the interval or it is testing the throttle rather than the reload.
void WaitPastThrottle() {
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
}

} // namespace

TEST(ReviewExternalReload, PicksUpAnItemAnotherProcessAppended) {
    const std::filesystem::path path = ScratchFile("appended");
    WriteItemsExternally(path, {MakeItem("existing", "Already here")});

    app::AppEvents events;
    app::controllers::ReviewController controller(events);
    std::string error;
    ASSERT_TRUE(controller.ConfigureStorage(path, error)) << error;
    ASSERT_EQ(controller.Items().size(), 1u);

    WriteItemsExternally(path, {MakeItem("existing", "Already here"), MakeItem("mcp-proposal:p1", "From MCP")});
    WaitPastThrottle();

    EXPECT_TRUE(controller.ReloadIfChangedExternally()) << "an externally appended review item was not noticed";
    ASSERT_EQ(controller.Items().size(), 2u);
    EXPECT_TRUE(controller.GetItemById("mcp-proposal:p1").has_value());
}

// Unsaved edits exist only in memory. Reloading over them would discard what the
// user typed, which is worse than showing an incoming comment a little late.
TEST(ReviewExternalReload, RefusesToReloadOverUnsavedEdits) {
    const std::filesystem::path path = ScratchFile("dirty");
    WriteItemsExternally(path, {MakeItem("existing", "Already here")});

    app::AppEvents events;
    app::controllers::ReviewController controller(events);
    std::string error;
    ASSERT_TRUE(controller.ConfigureStorage(path, error)) << error;

    controller.AddManualItem(MakeItem("mine", "Typed but not saved"));
    ASSERT_TRUE(controller.IsDirty());

    WriteItemsExternally(path, {MakeItem("existing", "Already here")});
    WaitPastThrottle();

    EXPECT_FALSE(controller.ReloadIfChangedExternally());
    EXPECT_TRUE(controller.GetItemById("mine").has_value())
        << "an external change discarded the user's unsaved review item";
}

// The frame loop calls this every frame; it must be nearly free when nothing has
// changed, and must not report a reload for the app's own state.
TEST(ReviewExternalReload, ReportsNoReloadWhenTheFileIsUntouched) {
    const std::filesystem::path path = ScratchFile("untouched");
    WriteItemsExternally(path, {MakeItem("existing", "Already here")});

    app::AppEvents events;
    app::controllers::ReviewController controller(events);
    std::string error;
    ASSERT_TRUE(controller.ConfigureStorage(path, error)) << error;

    WaitPastThrottle();
    EXPECT_FALSE(controller.ReloadIfChangedExternally());
    WaitPastThrottle();
    EXPECT_FALSE(controller.ReloadIfChangedExternally());
}
