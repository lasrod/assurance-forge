#include "app/controllers/project_controller.h"

#include "support/temp_files.h"
#include "ui/imgui_buffer_utils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace {

using test_support::TempDir;

} // namespace

TEST(ProjectControllerTest, BeginProjectFileCreateStoresKindNameAndShowsModal) {
    app::controllers::ProjectController controller;

    controller.BeginProjectFileCreate(app::ProjectFileCreateKind::EvidenceRegister, "evidence-register.af.json");

    EXPECT_EQ(controller.pending_project_file_kind, app::ProjectFileCreateKind::EvidenceRegister);
    EXPECT_STREQ(controller.project_file_name_buf, "evidence-register.af.json");
    EXPECT_TRUE(controller.show_project_file_name_modal);
}

TEST(ProjectControllerTest, RecentProjectsPreferenceRoundTrips) {
    app::controllers::ProjectController controller;
    std::vector<app::RecentProjectEntry> recent;
    app::RecentProjectEntry entry;
    entry.name = "Alpha";
    entry.path = "C:/tmp/alpha/af.proj";
    entry.claims = 2;
    app::TouchRecentProject(recent, entry);

    controller.LoadRecentProjectsPreference(app::SaveRecentProjectsPreference(recent));

    ASSERT_EQ(controller.recent_projects.size(), 1u);
    EXPECT_EQ(controller.recent_projects.front().name, "Alpha");
    EXPECT_FALSE(controller.RecentProjectsPreferenceJson().empty());
}

TEST(ProjectControllerTest, ScanDirectoryFindsXmlAndSelectsCurrentFile) {
    TempDir temp(test_support::UniqueTempDirectory("project_controller"));
    const std::filesystem::path selected = temp.path / "b.xml";
    std::ofstream(temp.path / "a.xml") << "<a/>";
    std::ofstream(selected) << "<b/>";
    std::ofstream(temp.path / "ignore.txt") << "ignore";

    app::controllers::ProjectController controller;
    ui::CopyToBuffer(controller.dir_path_buf, sizeof(controller.dir_path_buf), temp.path.string());
    ui::CopyToBuffer(controller.file_path_buf, sizeof(controller.file_path_buf), selected.string());

    controller.ScanDirectory();

    ASSERT_EQ(controller.xml_files.size(), 2u);
    ASSERT_GE(controller.selected_file_idx, 0);
    EXPECT_EQ(std::filesystem::path(controller.xml_files[controller.selected_file_idx]).filename(), "b.xml");
}
