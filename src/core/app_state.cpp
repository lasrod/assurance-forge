#include "core/app_state.h"

#include "core/audit/audit_store.h"
#include "core/derived_views.h"
#include "core/library_package_projection.h"
#include "core/project_file_io.h"
#include "core/project_service.h"
#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "core/terminology_text_utils.h"
#include "parser/model_utils.h"
#include "sacm/sacm_serializer.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <algorithm>
#include <exception>
#include <expected>
#include <new>
#include <unordered_map>
#include <utility>
#include <unordered_set>

namespace core {

bool AppState::load_file(const std::string& file_path) {
    try {
        // The SACM library is the SOLE load path: the projected POD model is what
        // the UI renders and `sacm_package` is a derived cache of the library. There
        // is NO legacy-parser fallback -- a file the library cannot read is a library
        // import bug to fix in the library, not a reason to keep a parallel legacy
        // parser in the app.
        library_document.reset();
        sacm_adapter::LoadOutcome outcome = sacm_adapter::load_document(file_path);
        if (!outcome.ok || outcome.document == nullptr) {
            active_project_file_role = ProjectFileRole::Unknown;
            active_project_file_path.clear();
            loaded_file_path.clear();
            has_unsaved_changes = false;
            loaded_case.reset();
            sacm_package.reset();
            ++case_revision;
            const std::string detail = sacm_adapter::summarize_load_diagnostics(outcome.diagnostics);
            status_message =
                "Error: the SACM library could not read this file" + (detail.empty() ? "." : ": " + detail);
            return false;
        }
        library_document = std::move(outcome.document);

        active_project_file_role = ProjectFileRole::Unknown;
        active_project_file_path.clear();
        loaded_file_path = std::filesystem::path(file_path);
        has_unsaved_changes = false;
        loaded_case = sacm_adapter::project_case(*library_document);
        ++case_revision;

        // `sacm_package` is the derived render/save cache, projected from the
        // library. The tag-carrying projection preserves ACP/confidence/GSN-role
        // tags; the terminology passes then hide/refresh the projected model.
        sacm_package = core::project_library_package_with_tags(*library_document);
        SynthesizeBareStrategyPlacements(loaded_case.value(), sacm_package.value());
        HideTerminologyArtifactReferences(loaded_case.value(), sacm_package.value());
        RefreshVisibleTerminologyContextDisplay(loaded_case.value(), sacm_package.value());
        status_message =
            "Loaded: " + loaded_case->name + " (" + std::to_string(loaded_case->elements.size()) + " elements)";

        return true;
    } catch (const std::bad_alloc&) {
        loaded_file_path.clear();
        has_unsaved_changes = false;
        loaded_case.reset();
        sacm_package.reset();
        library_document.reset();
        ++case_revision;
        status_message = "Error: ran out of memory while loading this file. It may be too large to open.";
        return false;
    } catch (const std::exception& error) {
        loaded_file_path.clear();
        has_unsaved_changes = false;
        loaded_case.reset();
        sacm_package.reset();
        library_document.reset();
        ++case_revision;
        status_message = std::string("Error: failed to load this file (") + error.what() + ").";
        return false;
    }
}

bool AppState::save_file(const std::string& file_path) {
    if (!sacm_package.has_value()) {
        status_message = "Error: No SACM data to save";
        return false;
    }

    // Phase 9 Stage 6: the library is the serialization source of truth, so
    // write library SACM XMI (falling back to the legacy serialization only if
    // the library round-trip fails). Note this round-trips the legacy package,
    // which already dropped truly-unknown/foreign XML at load; it preserves
    // everything the legacy structs model (ACP and other TaggedValues included)
    // but not unknown extension content -- no worse than the pre-Stage-6 save.
    const std::string bytes = core::library_xmi_from_package(sacm_package.value())
                                  .value_or(sacm::serialize_sacm(sacm_package.value()));
    if (WriteTextFileAtomic(file_path, bytes)) {
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
        std::filesystem::path save_path = loaded_file_path;
        if (save_path.empty() && active_project_file_role == ProjectFileRole::SacmArgument)
            save_path = active_project_file_path;
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
    ++case_revision;
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

    // Auto-migrate existing projects: ensure an audit store exists so the
    // history-timeline subsystem has a starting snapshot. The first
    // SacmArgument file is treated as the project's primary working SACM.
    for (const ProjectFileEntry& entry : current_project->files) {
        if (entry.role == ProjectFileRole::SacmArgument && !entry.relativePath.empty()) {
            audit::EnsureAuditStoreResult audit_result;
            std::string audit_error;
            if (!audit::EnsureAuditStore(current_project.value(), entry.relativePath, audit_result, audit_error)) {
                // Non-fatal: surface as a warning but allow the project to open.
                last_project_load_report.warnings.push_back("Audit store initialization failed: " + audit_error);
            }
            break;
        }
    }
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
    if (created_entry)
        *created_entry = entry;
    status_message = "Created: " + entry.relativePath.generic_string();

    // Ensure the audit store is initialized using the newly-created SACM as
    // snapshot 0. Failure here is non-fatal: the file is already on disk.
    audit::EnsureAuditStoreResult audit_result;
    std::string audit_error;
    if (!audit::EnsureAuditStore(current_project.value(), entry.relativePath, audit_result, audit_error)) {
        status_message += " (audit store init failed: " + audit_error + ")";
    }
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
    if (created_entry)
        *created_entry = entry;
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
    if (created_entry)
        *created_entry = entry;
    status_message = "Created: " + entry.relativePath.generic_string();
    return true;
}

bool AppState::open_project_file(const ProjectFileEntry& entry) {
    if (!current_project.has_value()) {
        status_message = "Create or open a project first.";
        return false;
    }
    const std::filesystem::path project_file_path = current_project->rootPath / entry.relativePath;
    if (entry.role != ProjectFileRole::SacmArgument) {
        active_project_file_role = entry.role;
        active_project_file_path = project_file_path;
        status_message = "Opened: " + entry.relativePath.generic_string();
        return true;
    }

    const ProjectFileRole previous_active_project_file_role = active_project_file_role;
    const std::filesystem::path previous_active_project_file_path = active_project_file_path;
    const std::filesystem::path previous_loaded_file_path = loaded_file_path;
    const bool previous_has_unsaved_changes = has_unsaved_changes;
    const auto previous_loaded_case = loaded_case;
    const auto previous_sacm_package = sacm_package;
    // The library document is move-only, so it is moved out and restored on
    // failure rather than copied.
    std::unique_ptr<sacm_adapter::LibraryDocument> previous_library_document =
        std::move(library_document);

    if (!load_file(project_file_path.string())) {
        active_project_file_role = previous_active_project_file_role;
        active_project_file_path = previous_active_project_file_path;
        loaded_file_path = previous_loaded_file_path;
        has_unsaved_changes = previous_has_unsaved_changes;
        loaded_case = previous_loaded_case;
        sacm_package = previous_sacm_package;
        library_document = std::move(previous_library_document);
        ++case_revision;
        return false;
    }

    active_project_file_role = entry.role;
    active_project_file_path = project_file_path;
    return true;
}

void AppState::sync_library_document() {
    if (library_document == nullptr || !sacm_package.has_value()) {
        return;
    }
    const std::string xml = sacm::serialize_sacm(sacm_package.value());
    sacm_adapter::reload_document(*library_document, xml);
}

} // namespace core
