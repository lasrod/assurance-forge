#pragma once

#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "core/app_state.h"
#include "core/project_service.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace app::controllers {

constexpr size_t kPathBufferSize = 512;

class ProjectController {
public:
    char file_path_buf[kPathBufferSize] = "data/oasc-ja.xml";
    char dir_path_buf[kPathBufferSize] = "data";
    std::vector<std::string> xml_files;
    int selected_file_idx = -1;
    bool show_overwrite_confirm = false;

    bool show_startup_project_window = true;
    bool show_create_project_modal = false;
    bool show_project_file_name_modal = false;
    bool show_save_before_project_file_open_modal = false;
    ProjectFileCreateKind pending_project_file_kind = ProjectFileCreateKind::Sacm;
    std::optional<core::ProjectFileEntry> pending_open_project_file_entry;
    char project_name_buf[128] = "MySafetyCase";
    char project_parent_buf[kPathBufferSize] = ".";
    char open_project_path_buf[kPathBufferSize] = "";
    char project_file_name_buf[256] = "main.sacm";
    // The SACM file the create-project dialog copies in as the first argument.
    // Empty means the dialog creates an empty project. Set by the "from existing
    // SACM" entry points, cleared when the dialog closes either way.
    std::filesystem::path pending_create_project_source_sacm;
    // The SACM file an `ImportedSacm` file-name dialog copies into the open
    // project.
    std::filesystem::path pending_import_sacm_source;
    std::vector<RecentProjectEntry> recent_projects;

    // Whether the name in the create dialog can be used, re-asked whenever it
    // changes rather than every frame: the parent can be a network share, where
    // a per-frame existence check is a per-frame round trip.
    core::CreateProjectObstacle create_project_obstacle = core::CreateProjectObstacle::None;
    // What a create actually refused, when one was attempted and failed. The
    // check above cannot see everything -- a folder appearing between the check
    // and the press, a permission the write finds out about -- and a dialog that
    // answers a press with nothing at all is the defect this exists to close.
    std::string create_project_error;
    // The same, for the add-a-file-to-the-project dialog, which had the same
    // silence: a name already in the project refused and said nothing.
    std::string create_project_file_error;

    // Re-asks the obstacle for what is currently typed. Called when the dialog
    // opens and on every edit of the name.
    void RefreshCreateProjectObstacle();

    void ScanDirectory();
    void BeginProjectFileCreate(ProjectFileCreateKind kind, const std::string& default_name);

    void LoadRecentProjectsPreference(const std::string& content);
    std::string RecentProjectsPreferenceJson() const;
    void TouchCurrentProjectRecent(const core::AppState& app_state);
    void RemoveRecentProjectByPath(const std::string& path);
};

} // namespace app::controllers