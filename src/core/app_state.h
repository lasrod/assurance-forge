#pragma once

#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

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

    // Monotonic counter bumped whenever the loaded document is loaded,
    // mutated, or otherwise invalidated. Render-side caches (e.g. the
    // per-tab workbench visible-case projection) compare this against a
    // stored value to detect when a rebuild can be skipped.
    std::uint64_t case_revision = 0;

    void bump_case_revision() {
        ++case_revision;
    }

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

} // namespace core
