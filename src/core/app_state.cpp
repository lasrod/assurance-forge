#include "core/app_state.h"

#include "core/audit/audit_store.h"
#include "core/library_package_projection.h"
#include "core/project_file_io.h"
#include "core/project_service.h"
#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "core/terminology_text_utils.h"
#include "parser/model_utils.h"
#include "sacm/sacm_parser.h"
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

namespace {

bool ReferencesAny(const std::vector<std::string>& refs, const std::unordered_set<std::string>& candidates) {
    return std::any_of(refs.begin(), refs.end(), [&](const std::string& ref) {
        return candidates.find(StripLeadingHash(ref)) != candidates.end();
    });
}

void AddRef(std::unordered_set<std::string>& refs, const std::string& ref) {
    if (!ref.empty())
        refs.insert(ref);
}

struct HiddenTerminologyRefs {
    std::unordered_set<std::string> artifact_reference_refs;
    std::unordered_set<std::string> asserted_context_refs;
};

bool ContextSourcesArtifactReference(const sacm::AssertedContext& context,
                                     const sacm::ArtifactReference& artifact_reference) {
    std::unordered_set<std::string> artifact_refs;
    AddRef(artifact_refs, artifact_reference.id);
    AddRef(artifact_refs, artifact_reference.gid);
    return ReferencesAny(context.sources, artifact_refs);
}

HiddenTerminologyRefs CollectHiddenTerminologyRefs(const sacm::AssuranceCasePackage& package) {
    HiddenTerminologyRefs refs;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (!IsTerminologyArtifactReference(package, artifact_reference))
                continue;
            if (!IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference)) {
                AddRef(refs.artifact_reference_refs, artifact_reference.id);
                AddRef(refs.artifact_reference_refs, artifact_reference.gid);
            }
            for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
                if (!ContextSourcesArtifactReference(context, artifact_reference) ||
                    IsVisibleTerminologyContext(context))
                    continue;
                AddRef(refs.asserted_context_refs, context.id);
                AddRef(refs.asserted_context_refs, context.gid);
            }
        }
    }
    return refs;
}

void HideTerminologyArtifactReferences(parser::AssuranceCase& model, const sacm::AssuranceCasePackage& package) {
    const HiddenTerminologyRefs hidden_refs = CollectHiddenTerminologyRefs(package);
    if (hidden_refs.artifact_reference_refs.empty() && hidden_refs.asserted_context_refs.empty())
        return;

    model.elements.erase(
        std::remove_if(model.elements.begin(),
                       model.elements.end(),
                       [&](const parser::SacmElement& element) {
                           if (element.type == "artifactreference") {
                               return hidden_refs.artifact_reference_refs.find(element.id) !=
                                          hidden_refs.artifact_reference_refs.end() ||
                                      hidden_refs.artifact_reference_refs.find(element.gid) !=
                                          hidden_refs.artifact_reference_refs.end();
                           }
                           return element.type == "assertedcontext" &&
                                  (hidden_refs.asserted_context_refs.find(element.id) !=
                                       hidden_refs.asserted_context_refs.end() ||
                                   hidden_refs.asserted_context_refs.find(element.gid) !=
                                       hidden_refs.asserted_context_refs.end() ||
                                   ReferencesAny(element.source_refs, hidden_refs.artifact_reference_refs));
                       }),
        model.elements.end());
}

void RefreshVisibleTerminologyContextDisplay(parser::AssuranceCase& model, const sacm::AssuranceCasePackage& package) {
    // Index elements by id and gid once so each artifact reference is an O(1) lookup instead of a
    // linear scan of every element (the loop below is otherwise O(references * elements)). The
    // index records the first occurrence per key and a lookup prefers the lowest matching index,
    // reproducing parser::FindElementByIdOrGid's "first element matching id or gid" behaviour.
    const std::size_t none = model.elements.size();
    std::unordered_map<std::string, std::size_t> first_index_by_id;
    std::unordered_map<std::string, std::size_t> first_index_by_gid;
    first_index_by_id.reserve(model.elements.size());
    first_index_by_gid.reserve(model.elements.size());
    for (std::size_t i = 0; i < model.elements.size(); ++i) {
        const parser::SacmElement& element = model.elements[i];
        if (!element.id.empty())
            first_index_by_id.emplace(element.id, i);
        if (!element.gid.empty())
            first_index_by_gid.emplace(element.gid, i);
    }
    const auto find_element = [&](const std::string& id, const std::string& gid) -> parser::SacmElement* {
        std::size_t best = none;
        if (!id.empty()) {
            const auto it = first_index_by_id.find(id);
            if (it != first_index_by_id.end())
                best = it->second;
        }
        if (!gid.empty()) {
            const auto it = first_index_by_gid.find(gid);
            if (it != first_index_by_gid.end())
                best = std::min(best, it->second);
        }
        return best == none ? nullptr : &model.elements[best];
    };

    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (!IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
                continue;
            parser::SacmElement* element = find_element(artifact_reference.id, artifact_reference.gid);
            if (!element)
                continue;
            const TerminologyTermReferenceResolution resolution =
                ResolveTerminologyTermReference(package, artifact_reference.referencedArtifact);
            if (!resolution.resolved || !resolution.term) {
                element->description.clear();
                element->description_langs.clear();
                continue;
            }

            element->name = TermContextDisplayLabel(*resolution.term);
            element->name_langs = resolution.term->name_ml.texts;
            if (element->name_langs.empty() && !element->name.empty())
                element->name_langs["en"] = element->name;
            element->description = resolution.term->description;
            element->description_langs = resolution.term->description_ml.texts;
            if (element->description_langs.empty() && !element->description.empty())
                element->description_langs["en"] = element->description;
        }
    }
}

} // namespace

bool AppState::load_file(const std::string& file_path) {
    try {
        // The SACM library is the load source of truth (Phase 9 Stage 4): the
        // projected POD model is what the UI renders. If the library cannot load
        // the file we fall back to the legacy parser so opening never breaks on
        // a library gap -- and say so, so the gap is visible rather than silent.
        library_document.reset();
        sacm_adapter::LoadOutcome outcome = sacm_adapter::load_document(file_path);
        std::optional<parser::AssuranceCase> projected;
        std::string load_note;
        if (outcome.ok && outcome.document != nullptr) {
            library_document = std::move(outcome.document);
            projected = sacm_adapter::project_case(*library_document);
        } else {
            std::expected<parser::AssuranceCase, std::string> legacy =
                parser::parse_sacm_xml(file_path);
            if (!legacy) {
                loaded_file_path.clear();
                has_unsaved_changes = false;
                loaded_case.reset();
                sacm_package.reset();
                status_message = "Error: " + std::move(legacy.error());
                return false;
            }
            projected = std::move(*legacy);
            load_note = " (loaded via legacy parser; the SACM library could not read this file)";
        }

        active_project_file_role = ProjectFileRole::Unknown;
        active_project_file_path.clear();
        loaded_file_path = std::filesystem::path(file_path);
        has_unsaved_changes = false;
        loaded_case = std::move(*projected);
        ++case_revision;

        // Also populate the SACM domain model for save support. `load_note` (the
        // legacy-parser-fallback warning) is appended to every branch, or a
        // library load gap would go silent whenever sacm::parse_sacm succeeds.
        const std::string summary =
            "Loaded: " + loaded_case->name + " (" + std::to_string(loaded_case->elements.size()) + " elements)";
        sacm_package.reset();
        if (auto sacm_result = sacm::parse_sacm(file_path)) {
            sacm_package = std::move(*sacm_result);
            HideTerminologyArtifactReferences(loaded_case.value(), sacm_package.value());
            RefreshVisibleTerminologyContextDisplay(loaded_case.value(), sacm_package.value());
            status_message = summary + load_note;
        } else {
            status_message = "Loaded with warning: " + loaded_case->name + " (" +
                             std::to_string(loaded_case->elements.size()) +
                             " elements), but save support is unavailable (SACM parse failed: " +
                             std::string(sacm_result.error()) + ")" + load_note;
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
