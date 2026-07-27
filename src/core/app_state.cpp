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
        // Cleared here, not in the success branch: a failed load must not leave the
        // previous file's warnings standing next to a status message about this one.
        load_warnings.clear();
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

        // A load that SUCCEEDS can still have told us something the user must
        // know -- most sharply, that the file is not a conformant SACM
        // interchange document and saving will not reproduce it (SACM-XMI-009,
        // an ODE container embedding SACM). These diagnostics used to be
        // discarded on the success path, so such a file opened silently and the
        // first save quietly rewrote it. The failure branch already surfaces
        // diagnostics; success must too, or "the library warns about this" is a
        // claim the product does not keep.
        //
        // Warning and above only: the Info stream is per-element reader detail,
        // and burying one real warning in fifty notices is the same as not
        // showing it.
        // Deduplicated BY CODE, because the raw stream is per-element: one real
        // file produces 222 warnings of which 220 are "this element had no
        // xmi:id, one was generated". Showing the first of those and calling it
        // the warning is the same as showing nothing.
        std::vector<std::string> seen_codes;
        for (const sacm_adapter::LoadDiagnostic& diagnostic : outcome.diagnostics) {
            if (diagnostic.severity == "Info")
                continue;
            if (std::find(seen_codes.begin(), seen_codes.end(), diagnostic.code) != seen_codes.end())
                continue;
            seen_codes.push_back(diagnostic.code);
            load_warnings.push_back(diagnostic.code + ": " + diagnostic.message);
        }

        // One code leads regardless of document order: SACM-XMI-009 says the
        // file is not a conformant SACM interchange document and that saving
        // will not reproduce it. Every other warning describes how we read the
        // file; that one describes what we are about to do to it, and the user
        // is deciding whether to keep working in this tool at all.
        const auto not_conformant =
            std::find_if(load_warnings.begin(), load_warnings.end(), [](const std::string& line) {
                return line.starts_with("SACM-XMI-009");
            });
        if (not_conformant != load_warnings.end())
            std::rotate(load_warnings.begin(), not_conformant, not_conformant + 1);

        if (!load_warnings.empty()) {
            status_message += " -- " + std::to_string(load_warnings.size()) +
                              (load_warnings.size() == 1 ? " warning: " : " warning kinds: ") +
                              load_warnings.front();
        }

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

    // Phase 9 Stage 7: serialize the LIBRARY-OWNED document itself, not a
    // projection of it. `sacm_package` is a derived cache whose legacy structs
    // model only what the application renders, so round-tripping through it
    // dropped everything the library preserved but the structs cannot hold --
    // unknown/foreign child elements and vendor-namespace attributes. The live
    // document has held every committed edit since the command flip, so saving
    // it is both current and lossless.
    //
    // The package fallback is defensive: the library is the sole load path, so a
    // loaded file always has a document. It is lossy for unknown content (see
    // core::library_xmi_from_package).
    std::string bytes;
    bool        serialized_from_library = false;
    if (library_document != nullptr) {
        sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*library_document);
        if (saved.ok) {
            bytes = std::move(saved.xml);
            serialized_from_library = true;
        }
    }
    if (!serialized_from_library) {
        bytes = core::library_xmi_from_package(sacm_package.value())
                    .value_or(sacm::serialize_sacm(sacm_package.value()));
    }
    if (WriteTextFileAtomic(file_path, bytes)) {
        loaded_file_path = std::filesystem::path(file_path);
        has_unsaved_changes = false;
        status_message = "Saved to: " + file_path;
        // The package fallback cannot carry unknown/foreign content the document
        // preserved, so a degraded save must be visible rather than silent.
        if (!serialized_from_library) {
            status_message += " (warning: saved a projection because the SACM library "
                              "document could not be serialized; unknown or vendor-specific "
                              "content was not preserved)";
        }
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
    // Preserving reload, not the plain one. This rebuilds the document from a
    // projection OF THAT DOCUMENT, and the legacy package has no field for
    // unknown/foreign XML -- so the plain reload erased preserved vendor
    // content here just as it did in the bridge. Reachable without a project:
    // a file opened outside one takes the no-bus dispatch branch, which calls
    // this after every command, and the next `save_file` then writes the
    // degraded document to disk.
    const std::string xml = sacm::serialize_sacm(sacm_package.value());
    sacm_adapter::reload_document_keeping_compatibility_content(*library_document, xml);
}

} // namespace core
