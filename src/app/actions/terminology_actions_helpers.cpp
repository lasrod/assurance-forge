#include "app/actions/terminology_actions_internal.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "core/string_utils.h"
#include "core/terminology_text_utils.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/ui_state.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace app::actions::detail {

using core::TrimWhitespace;
using ui::CopyToBuffer;

std::string TerminologySuggestionKey(const std::string& element_id, const std::string& term_value) {
    return element_id + "\n" + term_value;
}

void CopyTerminologyPackageToEditor(AppRuntimeState& state, const sacm::TerminologyPackage& package) {
    CopyToBuffer(state.terminology.package_name_buf, sizeof(state.terminology.package_name_buf), package.name);
    CopyToBuffer(state.terminology.package_description_buf,
                 sizeof(state.terminology.package_description_buf),
                 package.description);
}

void ClearTermEditorBuffers(AppRuntimeState& state) {
    CopyToBuffer(state.terminology.term_value_buf, sizeof(state.terminology.term_value_buf), "");
    CopyToBuffer(state.terminology.term_name_buf, sizeof(state.terminology.term_name_buf), "");
    CopyToBuffer(state.terminology.term_definition_buf, sizeof(state.terminology.term_definition_buf), "");
    CopyToBuffer(state.terminology.term_categories_buf, sizeof(state.terminology.term_categories_buf), "");
    CopyToBuffer(
        state.terminology.term_external_reference_buf, sizeof(state.terminology.term_external_reference_buf), "");
    CopyToBuffer(state.terminology.term_origin_buf, sizeof(state.terminology.term_origin_buf), "");
}

void CopyTermToEditor(AppRuntimeState& state, const sacm::Term& term) {
    CopyToBuffer(state.terminology.term_value_buf, sizeof(state.terminology.term_value_buf), term.value);
    CopyToBuffer(state.terminology.term_name_buf, sizeof(state.terminology.term_name_buf), term.name);
    CopyToBuffer(
        state.terminology.term_definition_buf, sizeof(state.terminology.term_definition_buf), term.description);
    CopyToBuffer(state.terminology.term_categories_buf,
                 sizeof(state.terminology.term_categories_buf),
                 core::JoinCategoryRefs(term.category_refs));
    CopyToBuffer(state.terminology.term_external_reference_buf,
                 sizeof(state.terminology.term_external_reference_buf),
                 term.externalReference);
    CopyToBuffer(state.terminology.term_origin_buf, sizeof(state.terminology.term_origin_buf), term.origin);
}

core::TerminologyTermDraft TermDraftFromEditor(const AppRuntimeState& state) {
    core::TerminologyTermDraft draft;
    draft.value = TrimWhitespace(state.terminology.term_value_buf);
    draft.name = TrimWhitespace(state.terminology.term_name_buf);
    draft.description = TrimWhitespace(state.terminology.term_definition_buf);
    draft.category_refs = core::SplitCategoryRefs(state.terminology.term_categories_buf);
    draft.externalReference = TrimWhitespace(state.terminology.term_external_reference_buf);
    draft.origin = TrimWhitespace(state.terminology.term_origin_buf);
    return draft;
}

void ClearCategoryEditorBuffers(AppRuntimeState& state) {
    CopyToBuffer(state.terminology.category_name_buf, sizeof(state.terminology.category_name_buf), "");
    CopyToBuffer(state.terminology.category_description_buf, sizeof(state.terminology.category_description_buf), "");
}

void CopyCategoryToEditor(AppRuntimeState& state, const sacm::Category& category) {
    CopyToBuffer(state.terminology.category_name_buf, sizeof(state.terminology.category_name_buf), category.name);
    CopyToBuffer(state.terminology.category_description_buf,
                 sizeof(state.terminology.category_description_buf),
                 category.description);
}

core::TerminologyCategoryDraft CategoryDraftFromEditor(const AppRuntimeState& state) {
    core::TerminologyCategoryDraft draft;
    draft.name = TrimWhitespace(state.terminology.category_name_buf);
    draft.description = TrimWhitespace(state.terminology.category_description_buf);
    return draft;
}

bool CategoryNameExists(const sacm::TerminologyPackage& package, const std::string& name) {
    return std::any_of(package.categories.begin(), package.categories.end(), [&](const sacm::Category& category) {
        return category.name == name;
    });
}

bool HasTerminologyPackageRef(const core::TerminologyPackageRef& package_ref) {
    return !package_ref.id.empty() || !package_ref.gid.empty();
}

bool TerminologyPackageMatchesRef(const sacm::TerminologyPackage& package,
                                  const core::TerminologyPackageRef& package_ref) {
    return (!package_ref.id.empty() && package.id == package_ref.id) ||
           (!package_ref.gid.empty() && package.gid == package_ref.gid);
}

core::TerminologyPackageRef TerminologyPackageRefFor(const sacm::TerminologyPackage& package) {
    return core::TerminologyPackageRef{package.id, package.gid};
}

bool ArgumentPackageContainsElement(const sacm::ArgumentPackage& argument_package, const std::string& element_id) {
    if (element_id.empty())
        return false;
    for (const auto& claim : argument_package.claims) {
        if (claim.id == element_id || claim.gid == element_id)
            return true;
    }
    for (const auto& reasoning : argument_package.argumentReasonings) {
        if (reasoning.id == element_id || reasoning.gid == element_id)
            return true;
    }
    for (const auto& artifact_reference : argument_package.artifactReferences) {
        if (artifact_reference.id == element_id || artifact_reference.gid == element_id)
            return true;
    }
    return false;
}

const sacm::ArgumentPackage* FindContainingArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                           const std::string& element_id) {
    for (const auto& argument_package : package.argumentPackages) {
        if (ArgumentPackageContainsElement(argument_package, element_id))
            return &argument_package;
    }
    return nullptr;
}

bool IsAssuranceCaseTerminologyPackage(const sacm::AssuranceCasePackage& package,
                                       const core::TerminologyPackageRef& package_ref) {
    for (const auto& terminology_package : package.terminologyPackages) {
        if (TerminologyPackageMatchesRef(terminology_package, package_ref))
            return true;
    }
    return false;
}

bool IsArgumentTerminologyPackage(const sacm::ArgumentPackage& argument_package,
                                  const core::TerminologyPackageRef& package_ref) {
    for (const auto& terminology_package : argument_package.terminologyPackages) {
        if (TerminologyPackageMatchesRef(terminology_package, package_ref))
            return true;
    }
    return false;
}

core::TerminologyPackageRef ResolveQuickDefineTargetPackage(const AppRuntimeState& state,
                                                            const std::string& element_id) {
    if (!state.app_state.sacm_package.has_value())
        return {};

    const sacm::AssuranceCasePackage& package = state.app_state.sacm_package.value();
    const sacm::ArgumentPackage* containing_argument_package = FindContainingArgumentPackage(package, element_id);
    if (HasTerminologyPackageRef(state.terminology.selected_package_ref) &&
        core::FindTerminologyPackage(package, state.terminology.selected_package_ref)) {
        if (IsAssuranceCaseTerminologyPackage(package, state.terminology.selected_package_ref) ||
            (containing_argument_package &&
             IsArgumentTerminologyPackage(*containing_argument_package, state.terminology.selected_package_ref))) {
            return state.terminology.selected_package_ref;
        }
    }

    if (containing_argument_package && !containing_argument_package->terminologyPackages.empty())
        return TerminologyPackageRefFor(containing_argument_package->terminologyPackages.front());
    if (!package.terminologyPackages.empty())
        return TerminologyPackageRefFor(package.terminologyPackages.front());
    return {};
}

QuickDefineTargetPackageResult EnsureQuickDefineTargetPackage(AppRuntimeState& state, const std::string& element_id) {
    QuickDefineTargetPackageResult target;
    target.package_ref = ResolveQuickDefineTargetPackage(state, element_id);
    if (HasTerminologyPackageRef(target.package_ref))
        return target;

    if (!state.app_state.sacm_package.has_value()) {
        target.error = "Open a SACM model before defining terms.";
        return target;
    }

    core::TerminologyPackageCreateResult created = core::CreateTerminologyPackage(
        state.app_state.sacm_package.value(), "Terminology Package", "Terms used by this safety case.");
    if (!created.success) {
        target.error = created.error;
        return target;
    }

    target.package_ref = created.package_ref;
    target.created = true;
    state.terminology.selected_package_ref = created.package_ref;
    state.terminology.selected_term_ref = core::TerminologyTermRef{};
    state.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    state.terminology.selected_package_file_path = state.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package =
            core::FindTerminologyPackage(state.app_state.sacm_package.value(), created.package_ref)) {
        CopyTerminologyPackageToEditor(state, *package);
    }
    return target;
}

void MarkTerminologyDocumentDirty(AppRuntimeState& state) {
    state.app_state.mark_dirty();
    state.document_dirty = true;
}

void InvalidateSacmPackageTreeCache(AppRuntimeState& state, const std::filesystem::path& relative_path) {
    state.sacm_package_tree_cache.erase(relative_path.generic_string());
}

bool CanSwitchProjectSacmFile(const core::AppState& app_state, const core::ProjectFileEntry& entry) {
    if (!app_state.current_project.has_value() || !app_state.has_unsaved_changes)
        return true;
    const std::filesystem::path target_path = app_state.current_project->rootPath / entry.relativePath;
    const std::filesystem::path current_sacm_path =
        !app_state.loaded_file_path.empty() ? app_state.loaded_file_path : app_state.active_project_file_path;
    return current_sacm_path.empty() || current_sacm_path == target_path;
}

bool IsActiveProjectSacmFile(const core::AppState& app_state, const core::ProjectFileEntry& entry) {
    if (!app_state.current_project.has_value())
        return false;
    const std::filesystem::path target_path = app_state.current_project->rootPath / entry.relativePath;
    if (!app_state.loaded_file_path.empty())
        return app_state.loaded_file_path == target_path;
    return !app_state.active_project_file_path.empty() && app_state.active_project_file_path == target_path;
}

bool EnsureProjectSacmFileOpen(AppRuntimeState& state, const core::ProjectFileEntry& entry, bool require_loaded_case) {
    if (IsActiveProjectSacmFile(state.app_state, entry) && state.app_state.sacm_package.has_value() &&
        (!require_loaded_case || state.app_state.loaded_case.has_value())) {
        return true;
    }
    return state.app_state.open_project_file(entry);
}

const sacm::ArtifactReference* FindArtifactReferenceById(const sacm::AssuranceCasePackage& package,
                                                         const std::string& artifact_reference_id) {
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (artifact_reference.id == artifact_reference_id || artifact_reference.gid == artifact_reference_id)
                return &artifact_reference;
        }
    }
    return nullptr;
}

const sacm::AssertedContext* FindAssertedContextById(const sacm::AssuranceCasePackage& package,
                                                     const std::string& asserted_context_id) {
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
            if (context.id == asserted_context_id || context.gid == asserted_context_id)
                return &context;
        }
    }
    return nullptr;
}

bool RefreshVisibleTerminologyContextProjection(core::AppState& app_state) {
    if (!app_state.loaded_case.has_value() || !app_state.sacm_package.has_value())
        return false;

    bool changed = false;
    parser::AssuranceCase& model = app_state.loaded_case.value();
    const sacm::AssuranceCasePackage& package = app_state.sacm_package.value();
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (!core::IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
                continue;
            parser::SacmElement* element =
                parser::FindElementByIdOrGid(model, artifact_reference.id, artifact_reference.gid);
            if (!element)
                continue;
            const core::TerminologyTermReferenceResolution resolution =
                core::ResolveTerminologyTermReference(package, artifact_reference.referencedArtifact);
            const std::string previous_name = element->name;
            const std::string previous_description = element->description;
            if (!resolution.resolved || !resolution.term) {
                element->description.clear();
                element->description_langs.clear();
            } else {
                element->name = core::TermContextDisplayLabel(*resolution.term);
                element->name_langs = resolution.term->name_ml.texts;
                if (element->name_langs.empty() && !element->name.empty())
                    element->name_langs["en"] = element->name;
                element->description = resolution.term->description;
                element->description_langs = resolution.term->description_ml.texts;
                if (element->description_langs.empty() && !element->description.empty())
                    element->description_langs["en"] = element->description;
            }
            changed = changed || element->name != previous_name || element->description != previous_description;
        }
    }
    return changed;
}

bool SyncVisibleTerminologyContextToParser(core::AppState& app_state,
                                           const core::TerminologyContextAssociationResult& result) {
    if (!app_state.loaded_case.has_value() || !app_state.sacm_package.has_value())
        return false;

    const sacm::ArtifactReference* artifact_reference =
        FindArtifactReferenceById(app_state.sacm_package.value(), result.artifact_reference_id);
    const sacm::AssertedContext* context =
        FindAssertedContextById(app_state.sacm_package.value(), result.asserted_context_id);
    if (!artifact_reference || !context || !core::IsVisibleTerminologyContext(*context))
        return false;

    parser::AssuranceCase& model = app_state.loaded_case.value();
    bool changed = false;
    if (!parser::FindElementByIdOrGid(model, artifact_reference->id, artifact_reference->gid)) {
        parser::SacmElement element;
        element.id = artifact_reference->id;
        element.gid = artifact_reference->gid;
        element.name = artifact_reference->name;
        element.type = "artifactreference";
        element.description = artifact_reference->description;
        element.name_langs = artifact_reference->name_ml.texts;
        element.description_langs = artifact_reference->description_ml.texts;
        model.elements.push_back(std::move(element));
        changed = true;
    }
    if (!parser::FindElementByIdOrGid(model, context->id, context->gid)) {
        parser::SacmElement element;
        element.id = context->id;
        element.gid = context->gid;
        element.name = context->name;
        element.type = "assertedcontext";
        element.description = context->description;
        element.name_langs = context->name_ml.texts;
        element.description_langs = context->description_ml.texts;
        element.source_refs = context->sources;
        element.target_refs = context->targets;
        element.assertion_declaration = context->assertionDeclaration;
        model.elements.push_back(std::move(element));
        changed = true;
    }
    return RefreshVisibleTerminologyContextProjection(app_state) || changed;
}

std::string TermStatusLabel(const sacm::AssuranceCasePackage& package,
                            const core::TerminologyPackageRef& package_ref,
                            const core::TerminologyTermRef& term_ref) {
    const sacm::Term* term = core::FindTerminologyTerm(package, package_ref, term_ref);
    if (!term)
        return "Term";
    if (!term->value.empty())
        return term->value;
    if (!term->name.empty())
        return term->name;
    return term->id.empty() ? "Term" : term->id;
}

namespace {

bool ReadStringField(const nlohmann::json& object, const char* key, std::string& value) {
    const auto field = object.find(key);
    if (field == object.end() || !field->is_string())
        return false;
    value = field->get<std::string>();
    return true;
}

} // namespace

bool DecodeTerminologyTermQuickFixPayload(const std::string& payload, TerminologyTermQuickFixPayload& decoded) {
    const nlohmann::json root = nlohmann::json::parse(payload, nullptr, false);
    if (!root.is_object() || root.size() != 5)
        return false;

    std::string package_id;
    std::string package_gid;
    std::string term_id;
    std::string term_gid;
    std::string term_value;
    if (!ReadStringField(root, "packageId", package_id) || !ReadStringField(root, "packageGid", package_gid) ||
        !ReadStringField(root, "termId", term_id) || !ReadStringField(root, "termGid", term_gid) ||
        !ReadStringField(root, "termValue", term_value)) {
        return false;
    }

    decoded.package_ref = core::TerminologyPackageRef{package_id, package_gid};
    decoded.term_ref = core::TerminologyTermRef{term_id, term_gid};
    decoded.term_value = term_value;
    return HasTerminologyPackageRef(decoded.package_ref) &&
           (!decoded.term_ref.id.empty() || !decoded.term_ref.gid.empty());
}

bool OpenTerminologyProblemTerm(AppRuntimeState& state,
                                const core::TerminologyPackageRef& package_ref,
                                const core::TerminologyTermRef& term_ref,
                                const std::string& filter_value) {
    if (!state.app_state.sacm_package.has_value())
        return false;
    const sacm::TerminologyPackage* package =
        core::FindTerminologyPackage(state.app_state.sacm_package.value(), package_ref);
    if (!package)
        return false;
    state.terminology.selected_package_ref = package_ref;
    state.terminology.selected_package_file_path = state.app_state.active_project_file_path;
    state.terminology.selected_term_ref = term_ref;
    state.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state.terminology.filter_buf, sizeof(state.terminology.filter_buf), filter_value);
    CopyToBuffer(state.terminology.category_filter_buf, sizeof(state.terminology.category_filter_buf), "");
    CopyTerminologyPackageToEditor(state, *package);
    state.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state.workbench.force_center_tab_selection = true;
    return true;
}

} // namespace app::actions::detail
