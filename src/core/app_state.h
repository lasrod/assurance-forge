#pragma once

#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"
#include "sacm_adapter/library_load.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace core {

// Simple application state container
struct AppState {
    // Currently loaded assurance case (if any)
    std::optional<parser::AssuranceCase> loaded_case;

    // SACM domain model (populated on load, still used for save, editing and
    // terminology display until later Phase 9 stages retire it).
    std::optional<sacm::AssuranceCasePackage> sacm_package;

    // The library-owned SACM document. As of Phase 9 Stage 4 this is the load
    // source of truth: `loaded_case` is projected from it. Null when the file
    // was loaded through the legacy parser fallback (a library load gap).
    std::unique_ptr<sacm_adapter::LibraryDocument> library_document;

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

    // Phase 9 Stage 5: re-derive `library_document` from the current
    // `sacm_package` so it stays consistent after an edit that did not route
    // through a library operation (e.g. the unaudited ACP controller path,
    // which bypasses the command bus and its re-derive net). No-op when either
    // is absent. Touches neither the audit log nor the saved file.
    void sync_library_document();

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
