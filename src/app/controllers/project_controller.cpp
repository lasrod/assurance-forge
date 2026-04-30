#include "app/controllers/project_controller.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace app::controllers {
namespace {

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0) return;
    const size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

}  // namespace

void ProjectController::ScanDirectory() {
    xml_files.clear();
    selected_file_idx = -1;

    std::error_code ec;
    if (!std::filesystem::is_directory(dir_path_buf, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path_buf, ec)) {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".xml") {
            xml_files.push_back(entry.path().string());
        }
    }

    std::sort(xml_files.begin(), xml_files.end());

    std::error_code path_ec;
    std::filesystem::path selected_path = std::filesystem::weakly_canonical(std::filesystem::path(file_path_buf), path_ec);
    if (path_ec) {
        selected_path = std::filesystem::path(file_path_buf).lexically_normal();
    }

    for (int i = 0; i < static_cast<int>(xml_files.size()); ++i) {
        std::filesystem::path candidate_path = std::filesystem::weakly_canonical(std::filesystem::path(xml_files[i]), path_ec);
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
    CopyToBuffer(project_file_name_buf, sizeof(project_file_name_buf), default_name);
    show_project_file_name_modal = true;
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

}  // namespace app::controllers