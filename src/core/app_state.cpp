#include "core/app_state.h"

#include "core/project_service.h"
#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "core/terminology_text_utils.h"
#include "parser/model_utils.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <algorithm>
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
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (!IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
                continue;
            parser::SacmElement* element =
                parser::FindElementByIdOrGid(model, artifact_reference.id, artifact_reference.gid);
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
    parser::ParseResult result = parser::parse_sacm_xml(file_path);

    if (result.success) {
        active_project_file_role = ProjectFileRole::Unknown;
        active_project_file_path.clear();
        loaded_file_path = std::filesystem::path(file_path);
        has_unsaved_changes = false;
        loaded_case = std::move(result.assurance_case);
        status_message =
            "Loaded: " + loaded_case->name + " (" + std::to_string(loaded_case->elements.size()) + " elements)";

        // Also populate the SACM domain model for save support
        auto sacm_result = sacm::parse_sacm(file_path);
        if (sacm_result.success) {
            sacm_package = std::move(sacm_result.package);
            HideTerminologyArtifactReferences(loaded_case.value(), sacm_package.value());
            RefreshVisibleTerminologyContextDisplay(loaded_case.value(), sacm_package.value());
            status_message =
                "Loaded: " + loaded_case->name + " (" + std::to_string(loaded_case->elements.size()) + " elements)";
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
    if (created_entry)
        *created_entry = entry;
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

    if (!load_file(project_file_path.string())) {
        active_project_file_role = previous_active_project_file_role;
        active_project_file_path = previous_active_project_file_path;
        loaded_file_path = previous_loaded_file_path;
        has_unsaved_changes = previous_has_unsaved_changes;
        loaded_case = previous_loaded_case;
        sacm_package = previous_sacm_package;
        return false;
    }

    active_project_file_role = entry.role;
    active_project_file_path = project_file_path;
    return true;
}

} // namespace core
