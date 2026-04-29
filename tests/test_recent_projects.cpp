#include <gtest/gtest.h>

#include "app/recent_projects.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

app::RecentProjectEntry Entry(const std::string& name, const std::filesystem::path& path, int claims) {
    app::RecentProjectEntry entry;
    entry.name = name;
    entry.path = path.u8string();
    entry.claims = claims;
    entry.strategies = claims + 1;
    entry.evidence = claims + 2;
    entry.undeveloped = claims + 3;
    return entry;
}

}  // namespace

TEST(RecentProjectsTest, InvalidPreferenceReturnsEmptyList) {
    EXPECT_TRUE(app::LoadRecentProjectsPreference("not json").empty());
    EXPECT_TRUE(app::LoadRecentProjectsPreference(R"({"recentProjects":"bad"})").empty());
}

TEST(RecentProjectsTest, SavesAndLoadsRoundTrip) {
    std::vector<app::RecentProjectEntry> recent;
    app::TouchRecentProject(recent, Entry("Alpha", std::filesystem::temp_directory_path() / "alpha" / "af.proj", 7));
    app::TouchRecentProject(recent, Entry("Beta", std::filesystem::temp_directory_path() / "beta" / "af.proj", 3));

    std::vector<app::RecentProjectEntry> loaded =
        app::LoadRecentProjectsPreference(app::SaveRecentProjectsPreference(recent));

    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].name, "Beta");
    EXPECT_EQ(loaded[0].claims, 3);
    EXPECT_EQ(loaded[0].strategies, 4);
    EXPECT_EQ(loaded[0].evidence, 5);
    EXPECT_EQ(loaded[0].undeveloped, 6);
    EXPECT_EQ(loaded[1].name, "Alpha");
}

TEST(RecentProjectsTest, TouchMovesDuplicateToFront) {
    const std::filesystem::path manifest = std::filesystem::temp_directory_path() / "same" / "af.proj";

    std::vector<app::RecentProjectEntry> recent;
    app::TouchRecentProject(recent, Entry("Original", manifest, 1));
    app::TouchRecentProject(recent, Entry("Other", std::filesystem::temp_directory_path() / "other" / "af.proj", 2));
    app::TouchRecentProject(recent, Entry("Updated", manifest, 9));

    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0].name, "Updated");
    EXPECT_EQ(recent[0].claims, 9);
    EXPECT_EQ(recent[1].name, "Other");
}

TEST(RecentProjectsTest, KeepsOnlyLatestFive) {
    std::vector<app::RecentProjectEntry> recent;
    for (int index = 0; index < 7; ++index) {
        app::TouchRecentProject(
            recent,
            Entry("Project" + std::to_string(index),
                  std::filesystem::temp_directory_path() / ("project" + std::to_string(index)) / "af.proj",
                  index));
    }

    ASSERT_EQ(recent.size(), app::kMaxRecentProjects);
    EXPECT_EQ(recent.front().name, "Project6");
    EXPECT_EQ(recent.back().name, "Project2");
}

TEST(RecentProjectsTest, RemoveDeletesMatchingPath) {
    const std::filesystem::path keep = std::filesystem::temp_directory_path() / "keep" / "af.proj";
    const std::filesystem::path remove = std::filesystem::temp_directory_path() / "remove" / "af.proj";

    std::vector<app::RecentProjectEntry> recent;
    app::TouchRecentProject(recent, Entry("Keep", keep, 1));
    app::TouchRecentProject(recent, Entry("Remove", remove, 2));

    app::RemoveRecentProject(recent, remove.u8string());

    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].name, "Keep");
}
