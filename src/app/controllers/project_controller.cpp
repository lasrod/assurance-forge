#include "app/controllers/project_controller.h"

#include "core/project_service.h"
#include "core/string_utils.h"
#include "ui/imgui_buffer_utils.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace app::controllers {

void ProjectController::ScanDirectory() {
    xml_files.clear();
    selected_file_idx = -1;

    std::error_code ec;
    if (!std::filesystem::is_directory(dir_path_buf, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path_buf, ec)) {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        ext = core::ToLower(std::move(ext));
        if (ext == ".xml") {
            xml_files.push_back(entry.path().string());
        }
    }

    std::sort(xml_files.begin(), xml_files.end());

    std::error_code path_ec;
    std::filesystem::path selected_path =
        std::filesystem::weakly_canonical(std::filesystem::path(file_path_buf), path_ec);
    if (path_ec) {
        selected_path = std::filesystem::path(file_path_buf).lexically_normal();
    }

    for (int i = 0; i < static_cast<int>(xml_files.size()); ++i) {
        std::filesystem::path candidate_path =
            std::filesystem::weakly_canonical(std::filesystem::path(xml_files[i]), path_ec);
        if (path_ec) {
            path_ec.clear();
            candidate_path = std::filesystem::path(xml_files[i]).lexically_normal();
        }

        if (candidate_path == selected_path) {
            selected_file_idx = i;
            break;
        }
    }
}

void ProjectController::BeginProjectFileCreate(ProjectFileCreateKind kind, const std::string& default_name) {
    pending_project_file_kind = kind;
    ui::CopyToBuffer(project_file_name_buf, sizeof(project_file_name_buf), default_name);
    create_project_file_error.clear();
    show_project_file_name_modal = true;
}

void ProjectController::RefreshCreateProjectObstacle() {
    create_project_obstacle = core::ProjectService::FindCreateProjectObstacle(project_name_buf, project_parent_buf);
    // A stale refusal must not outlive the name it was about: the user's next
    // keystroke is them answering it.
    create_project_error.clear();
}

void ProjectController::LoadRecentProjectsPreference(const std::string& content) {
    recent_projects = app::LoadRecentProjectsPreference(content);
}

std::string ProjectController::RecentProjectsPreferenceJson() const {
    return app::SaveRecentProjectsPreference(recent_projects);
}

void ProjectController::TouchCurrentProjectRecent(const core::AppState& app_state) {
    RecentProjectEntry entry = MakeRecentProjectEntry(app_state);
    if (!entry.path.empty()) {
        TouchRecentProject(recent_projects, std::move(entry));
    }
}

void ProjectController::RemoveRecentProjectByPath(const std::string& path) {
    app::RemoveRecentProject(recent_projects, path);
}

} // namespace app::controllers