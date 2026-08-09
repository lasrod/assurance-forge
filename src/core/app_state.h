#pragma once

#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"
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

    // The SACM package projected from the library-owned document. Currently returns the
    // cached `sacm_package` field; a later Phase 3 slice will compute it from
    // `library_document` and delete the field. Callers must guard with has_projected_package().
    const sacm::AssuranceCasePackage& projected_package() const {
        // clang-tidy reports this as an unchecked optional access. It is not:
        // `.value()` throws `std::bad_optional_access` on a broken precondition,
        // where `operator*` would be undefined behaviour. The check does not
        // distinguish the two, and the throw is the backstop that makes the
        // documented precondition above safe to state rather than enforce.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        return sacm_package.value();
    }
    bool has_projected_package() const {
        return sacm_package.has_value();
    }

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

    // Warning-and-above diagnostics from the last successful load, as display
    // lines. A load can succeed and still have something the user must know --
    // that the file is not a conformant SACM interchange document and saving
    // will not reproduce it, for one (SACM-XMI-009). `status_message` carries
    // the first; this holds all of them for a panel that wants to list them.
    // Cleared by every load.
    std::vector<std::string> load_warnings;

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
    // Brings `af.proj`'s recorded hashes for one tracked file back in line with
    // what is on disk, and writes the manifest.
    //
    // Called after saving, because saving is what makes the recorded hash wrong.
    // A project that reports its own save as an external modification trains the
    // user to dismiss the one warning that would tell them a colleague had
    // changed a safety argument underneath them.
    bool refresh_tracked_file_hashes(const std::filesystem::path& file_path);
    void mark_dirty();

    bool create_empty_project(const std::string& project_name, const std::string& parent_location);
    bool open_project(const std::string& project_or_manifest_path);
    bool create_project_sacm_file(const std::string& file_name, ProjectFileEntry* created_entry = nullptr);
    bool create_project_evidence_register(const std::string& file_name, ProjectFileEntry* created_entry = nullptr);
    bool create_project_j3377_cae_register(const std::string& file_name, ProjectFileEntry* created_entry = nullptr);
    bool open_project_file(const ProjectFileEntry& entry);
};

} // namespace core
