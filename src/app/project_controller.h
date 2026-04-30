#pragma once

#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "core/app_state.h"

#include <cstddef>
#include <string>
#include <vector>

namespace app {

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
    ProjectFileCreateKind pending_project_file_kind = ProjectFileCreateKind::Sacm;
    char project_name_buf[128] = "MySafetyCase";
    char project_parent_buf[kPathBufferSize] = ".";
    char open_project_path_buf[kPathBufferSize] = "";
    char project_file_name_buf[256] = "main.sacm";
    std::vector<RecentProjectEntry> recent_projects;

    void ScanDirectory();
    void BeginProjectFileCreate(ProjectFileCreateKind kind, const std::string& default_name);

    void LoadRecentProjectsPreference(const std::string& content);
    std::string RecentProjectsPreferenceJson() const;
    void TouchCurrentProjectRecent(const core::AppState& app_state);
    void RemoveRecentProjectByPath(const std::string& path);
};

}  // namespace app