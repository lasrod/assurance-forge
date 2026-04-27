#pragma once

#include <string>
#include <optional>
#include <filesystem>
#include "parser/xml_parser.h"
#include "core/project_model.h"
#include "sacm/sacm_model.h"

namespace core {

// Simple application state container
struct AppState {
    // Currently loaded assurance case (if any)
    std::optional<parser::AssuranceCase> loaded_case;

    // SACM domain model (populated on load, used for save)
    std::optional<sacm::AssuranceCasePackage> sacm_package;

    // Currently open Assurance Forge project (if any)
    std::optional<AssuranceProject> current_project;

    // Last project load/create validation report, shown by the runtime as a popup.
    ProjectLoadReport last_project_load_report;

    ProjectFileRole active_project_file_role = ProjectFileRole::Unknown;
    std::filesystem::path active_project_file_path;
    std::filesystem::path loaded_file_path;
    bool has_unsaved_changes = false;

    // Status message for UI display
    std::string status_message;

    // Load an assurance case from file
    bool load_file(const std::string& file_path);

    // Save the SACM package to file
    bool save_file(const std::string& file_path);
    bool save_current_document();
    bool save_project();
    void mark_dirty();

    bool create_empty_project(const std::string& project_name, const std::string& parent_location);
    bool open_project(const std::string& project_or_manifest_path);
    bool create_project_sacm_file(const std::string& file_name, ProjectFileEntry* created_entry = nullptr);
    bool create_project_evidence_register(const std::string& file_name, ProjectFileEntry* created_entry = nullptr);
    bool create_project_j3377_cae_register(const std::string& file_name, ProjectFileEntry* created_entry = nullptr);
    bool open_project_file(const ProjectFileEntry& entry);
};

}  // namespace core
