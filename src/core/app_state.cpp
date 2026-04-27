#include "core/app_state.h"
#include "core/project_service.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

namespace core {

bool AppState::load_file(const std::string& file_path) {
    parser::ParseResult result = parser::parse_sacm_xml(file_path);

    if (result.success) {
        active_project_file_role = ProjectFileRole::Unknown;
        active_project_file_path.clear();
        loaded_file_path = std::filesystem::path(file_path);
        has_unsaved_changes = false;
        loaded_case = std::move(result.assurance_case);
        status_message = "Loaded: " + loaded_case->name +
                         " (" + std::to_string(loaded_case->elements.size()) + " elements)";

        // Also populate the SACM domain model for save support
        auto sacm_result = sacm::parse_sacm(file_path);
        if (sacm_result.success) {
            sacm_package = std::move(sacm_result.package);
        }

        return true;
    } else {
        loaded_file_path.clear();
        has_unsaved_changes = false;
        loaded_case.reset();
        sacm_package.reset();
        status_message = "Error: " + result.error_message;
        return false;
    }
}

bool AppState::save_file(const std::string& file_path) {
    if (!sacm_package.has_value()) {
        status_message = "Error: No SACM data to save";
        return false;
    }

    if (sacm::serialize_sacm_to_file(sacm_package.value(), file_path)) {
        loaded_file_path = std::filesystem::path(file_path);
        has_unsaved_changes = false;
        status_message = "Saved to: " + file_path;
        return true;
    } else {
        status_message = "Error: Failed to write " + file_path;
        return false;
    }
}

bool AppState::save_current_document() {
    if (loaded_file_path.empty()) {
        status_message = "Error: No file path available for save.";
        return false;
    }
    return save_file(loaded_file_path.string());
}

bool AppState::save_project() {
    if (!current_project.has_value()) {
        status_message = "Create or open a project first.";
        return false;
    }

    if (has_unsaved_changes) {
        std::filesystem::path save_path = active_project_file_path;
        if (save_path.empty()) save_path = loaded_file_path;
        if (save_path.empty()) {
            status_message = "Error: Could not determine which file to save.";
            return false;
        }
        if (!save_file(save_path.string())) {
            return false;
        }
    }

    ProjectService::RefreshFileStatus(current_project.value());

    std::string error;
    if (!ProjectService::WriteManifestSafely(current_project.value(), error)) {
        status_message = "Project save failed: " + error;
        return false;
    }

    status_message = "Project saved: " + current_project->name;
    return true;
}

void AppState::mark_dirty() {
    has_unsaved_changes = true;
}

bool AppState::create_empty_project(const std::string& project_name, const std::string& parent_location) {
    AssuranceProject project;
    ProjectLoadReport report;
    std::string error;
    if (!ProjectService::CreateEmptyProject(project_name, parent_location, project, report, error)) {
        status_message = "Project create failed: " + error;
        last_project_load_report = report;
        return false;
    }
    current_project = std::move(project);
    last_project_load_report = std::move(report);
    status_message = "Created project: " + current_project->name;
    return true;
}

bool AppState::open_project(const std::string& project_or_manifest_path) {
    AssuranceProject project;
    ProjectLoadReport report;
    std::string error;
    if (!ProjectService::OpenProject(project_or_manifest_path, project, report, error)) {
        status_message = "Project open failed: " + error;
        last_project_load_report = std::move(report);
        return false;
    }
    current_project = std::move(project);
    last_project_load_report = std::move(report);
    status_message = "Opened project: " + current_project->name;
    return true;
}

bool AppState::create_project_sacm_file(const std::string& file_name, ProjectFileEntry* created_entry) {
    if (!current_project.has_value()) {
        status_message = "Create or open a project first.";
        return false;
    }
    ProjectFileEntry entry;
    std::string error;
    if (!ProjectService::AddSacmFile(current_project.value(), file_name, entry, error)) {
        status_message = "SACM file create failed: " + error;
        return false;
    }
    if (created_entry) *created_entry = entry;
    status_message = "Created: " + entry.relativePath.generic_string();
    return true;
}

bool AppState::create_project_evidence_register(const std::string& file_name, ProjectFileEntry* created_entry) {
    if (!current_project.has_value()) {
        status_message = "Create or open a project first.";
        return false;
    }
    ProjectFileEntry entry;
    std::string error;
    if (!ProjectService::AddEvidenceRegister(current_project.value(), file_name, entry, error)) {
        status_message = "Evidence register create failed: " + error;
        return false;
    }
    if (created_entry) *created_entry = entry;
    status_message = "Created: " + entry.relativePath.generic_string();
    return true;
}

bool AppState::create_project_j3377_cae_register(const std::string& file_name, ProjectFileEntry* created_entry) {
    if (!current_project.has_value()) {
        status_message = "Create or open a project first.";
        return false;
    }
    ProjectFileEntry entry;
    std::string error;
    if (!ProjectService::AddJ3377CaeRegister(current_project.value(), file_name, entry, error)) {
        status_message = "J3377 CAE register create failed: " + error;
        return false;
    }
    if (created_entry) *created_entry = entry;
    status_message = "Created: " + entry.relativePath.generic_string();
    return true;
}

bool AppState::open_project_file(const ProjectFileEntry& entry) {
    if (!current_project.has_value()) {
        status_message = "Create or open a project first.";
        return false;
    }
    active_project_file_role = entry.role;
    active_project_file_path = current_project->rootPath / entry.relativePath;
    if (entry.role != ProjectFileRole::SacmArgument) {
        status_message = "Opened: " + entry.relativePath.generic_string();
        return true;
    }
    return load_file((current_project->rootPath / entry.relativePath).string());
}

}  // namespace core
