#include "app/controllers/project_controller.h"

#include "support/temp_files.h"
#include "ui/imgui_buffer_utils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
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

// The import dialog's button is held while the typed name is taken, and the
// hold lifts as soon as the name is free -- the reported dead press was an
// import that could only fail, offered with nothing said until it was pressed.
TEST(ProjectControllerTest, ProjectFileObstacleFollowsTheTypedName) {
    TempDir tmp(test_support::UniqueTempDirectory("project_controller"));
    std::optional<core::AssuranceProject> project = core::AssuranceProject{};
    core::ProjectLoadReport report;
    std::string error;
    ASSERT_TRUE(core::ProjectService::CreateEmptyProject("Target", tmp.path, *project, report, error)) << error;

    app::controllers::ProjectController controller;
    controller.pending_import_sacm_source = tmp.path / "elsewhere" / "main.sacm";
    controller.BeginProjectFileCreate(app::ProjectFileCreateKind::ImportedSacm, "main.sacm");
    controller.create_project_file_error = "stale refusal";

    controller.RefreshProjectFileObstacle(project);
    EXPECT_EQ(controller.project_file_obstacle, core::ProjectFileObstacle::AlreadyTracked);
    EXPECT_EQ(controller.planned_project_file_path.generic_string(), "arguments/main.sacm");
    EXPECT_TRUE(controller.create_project_file_error.empty()) << "a refusal must not outlive the name it was about";

    ui::CopyToBuffer(controller.project_file_name_buf, sizeof(controller.project_file_name_buf), "boundaries");
    controller.RefreshProjectFileObstacle(project);
    EXPECT_EQ(controller.project_file_obstacle, core::ProjectFileObstacle::None);
    EXPECT_EQ(controller.planned_project_file_path.generic_string(), "arguments/boundaries.sacm");

    // Without a project there is nothing to clash with, and the add itself
    // reports that no project is open.
    controller.RefreshProjectFileObstacle(std::nullopt);
    EXPECT_EQ(controller.project_file_obstacle, core::ProjectFileObstacle::None);
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
