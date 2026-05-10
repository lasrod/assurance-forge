#include "app/actions/terminology_actions.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "parser/xml_parser.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace app::actions {
namespace {

void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0)
        return;
    const size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}

std::string TerminologySuggestionKey(const std::string& element_id, const std::string& term_value) {
    return element_id + "\n" + term_value;
}

std::string JoinCategoryRefs(const std::vector<std::string>& refs) {
    std::string result;
    for (const auto& ref : refs) {
        if (ref.empty())
            continue;
        if (!result.empty())
            result += ", ";
        result += ref;
    }
    return result;
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
                 JoinCategoryRefs(term.category_refs));
    CopyToBuffer(state.terminology.term_external_reference_buf,
                 sizeof(state.terminology.term_external_reference_buf),
                 term.externalReference);
    CopyToBuffer(state.terminology.term_origin_buf, sizeof(state.terminology.term_origin_buf), term.origin);
}

std::vector<std::string> SplitCategoryRefs(const std::string& raw) {
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::stringstream stream(normalized);
    std::vector<std::string> refs;
    std::string item;
    while (stream >> item) {
        item = TrimWhitespace(item);
        if (!item.empty() && std::find(refs.begin(), refs.end(), item) == refs.end())
            refs.push_back(item);
    }
    return refs;
}

core::TerminologyTermDraft TermDraftFromEditor(const AppRuntimeState& state) {
    core::TerminologyTermDraft draft;
    draft.value = TrimWhitespace(state.terminology.term_value_buf);
    draft.name = TrimWhitespace(state.terminology.term_name_buf);
    draft.description = TrimWhitespace(state.terminology.term_definition_buf);
    draft.category_refs = SplitCategoryRefs(state.terminology.term_categories_buf);
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
    return app_state.active_project_file_path.empty() || app_state.active_project_file_path == target_path;
}

bool IsActiveProjectSacmFile(const core::AppState& app_state, const core::ProjectFileEntry& entry) {
    if (!app_state.current_project.has_value() || app_state.active_project_file_path.empty())
        return false;
    return app_state.active_project_file_path == app_state.current_project->rootPath / entry.relativePath;
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

bool ParserModelHasElement(const parser::AssuranceCase& model, const std::string& id, const std::string& gid) {
    return std::any_of(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return (!id.empty() && element.id == id) || (!gid.empty() && element.gid == gid);
    });
}

parser::SacmElement* FindParserElement(parser::AssuranceCase& model, const std::string& id, const std::string& gid) {
    for (parser::SacmElement& element : model.elements) {
        if ((!id.empty() && element.id == id) || (!gid.empty() && element.gid == gid))
            return &element;
    }
    return nullptr;
}

std::string TermContextDisplayLabel(const sacm::Term& term) {
    if (term.value.empty())
        return term.name.empty() ? term.id : term.name;
    if (term.name.empty() || term.name == term.value)
        return term.value;
    return term.value + ": " + term.name;
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
            parser::SacmElement* element = FindParserElement(model, artifact_reference.id, artifact_reference.gid);
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
                element->name = TermContextDisplayLabel(*resolution.term);
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
    if (!ParserModelHasElement(model, artifact_reference->id, artifact_reference->gid)) {
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
    if (!ParserModelHasElement(model, context->id, context->gid)) {
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

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

struct TerminologyTermQuickFixPayload {
    core::TerminologyPackageRef package_ref;
    core::TerminologyTermRef term_ref;
    std::string term_value;
};

bool DecodeTerminologyTermQuickFixPayload(const std::string& payload, TerminologyTermQuickFixPayload& decoded) {
    std::stringstream stream(payload);
    std::vector<std::string> parts;
    std::string part;
    while (std::getline(stream, part)) {
        parts.push_back(part);
    }
    if (parts.size() < 5)
        return false;
    decoded.package_ref = core::TerminologyPackageRef{parts[0], parts[1]};
    decoded.term_ref = core::TerminologyTermRef{parts[2], parts[3]};
    decoded.term_value = parts[4];
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

} // namespace

TerminologyActions::TerminologyActions(AppRuntimeState& state) : state_(state) {}

void TerminologyActions::BeginAddPackage(const core::ProjectFileEntry& entry,
                                         const sacm::SacmPackageTreeNode& parent_node) {
    if (parent_node.type != sacm::SacmPackageNodeType::AssuranceCasePackage)
        return;
    if (!CanSwitchProjectSacmFile(state_.app_state, entry)) {
        SetStatus(state_, "Save the current SACM file before adding a terminology package.");
        return;
    }

    state_.terminology.pending_package_parent_entry = entry;
    CopyToBuffer(state_.terminology.new_package_name_buf,
                 sizeof(state_.terminology.new_package_name_buf),
                 "Terminology Package");
    CopyToBuffer(
        state_.terminology.new_package_description_buf, sizeof(state_.terminology.new_package_description_buf), "");
    state_.terminology.show_create_package_modal = true;
}

bool TerminologyActions::ConfirmAddPackage() {
    if (!state_.terminology.pending_package_parent_entry.has_value()) {
        state_.terminology.show_create_package_modal = false;
        return false;
    }

    const core::ProjectFileEntry entry = state_.terminology.pending_package_parent_entry.value();
    if (!CanSwitchProjectSacmFile(state_.app_state, entry)) {
        SetStatus(state_, "Save the current SACM file before adding a terminology package.");
        return false;
    }
    if (!EnsureProjectSacmFileOpen(state_, entry, false))
        return false;
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Could not load an editable SACM package model.");
        return false;
    }

    core::TerminologyPackageCreateResult result =
        core::CreateTerminologyPackage(state_.app_state.sacm_package.value(),
                                       TrimWhitespace(state_.terminology.new_package_name_buf),
                                       TrimWhitespace(state_.terminology.new_package_description_buf));
    if (!result.success) {
        SetStatus(state_, "Terminology package create failed: " + result.error);
        return false;
    }

    state_.terminology.selected_package_ref = result.package_ref;
    state_.terminology.selected_term_ref = core::TerminologyTermRef{};
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package =
            core::FindTerminologyPackage(state_.app_state.sacm_package.value(), result.package_ref)) {
        CopyTerminologyPackageToEditor(state_, *package);
    }
    MarkTerminologyDocumentDirty(state_);
    InvalidateSacmPackageTreeCache(state_, entry.relativePath);
    state_.terminology.show_create_package_modal = false;
    state_.terminology.pending_package_parent_entry.reset();
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Added terminology package " + result.package_ref.id + ".");
    return true;
}

bool TerminologyActions::ApplyPackageEdits() {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    std::string error;
    if (!core::UpdateTerminologyPackage(state_.app_state.sacm_package.value(),
                                        state_.terminology.selected_package_ref,
                                        TrimWhitespace(state_.terminology.package_name_buf),
                                        TrimWhitespace(state_.terminology.package_description_buf),
                                        error)) {
        SetStatus(state_, "Terminology package update failed: " + error);
        return false;
    }

    MarkTerminologyDocumentDirty(state_);
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    return true;
}

void TerminologyActions::BeginDeletePackage() {
    state_.terminology.show_delete_package_modal = true;
}

bool TerminologyActions::ConfirmDeletePackage() {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    std::string error;
    if (!core::DeleteTerminologyPackage(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, error)) {
        SetStatus(state_, "Terminology package delete failed: " + error);
        return false;
    }

    MarkTerminologyDocumentDirty(state_);
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    state_.terminology.selected_package_ref = core::TerminologyPackageRef{};
    state_.terminology.show_delete_package_modal = false;
    state_.workbench.show_terminology_package_tab = false;
    ui::GetUiState().center_view = ui::CenterView::PackageDetails;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Deleted terminology package.");
    return true;
}

bool TerminologyActions::OpenTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before opening terminology terms.");
        return false;
    }

    const sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(state_.app_state.sacm_package.value(), package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return false;
    }
    const sacm::Term* term = core::FindTerminologyTerm(*terminology_package, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return false;
    }

    state_.terminology.selected_package_ref = package_ref;
    state_.terminology.selected_term_ref = term_ref;
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    CopyTerminologyPackageToEditor(state_, *terminology_package);
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), term->value);
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Opened term " + term->value + ".");
    return true;
}

bool TerminologyActions::EditTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    if (!OpenTermFromCanvas(package_ref, term_ref))
        return false;
    if (!state_.app_state.sacm_package.has_value())
        return false;
    const sacm::Term* term = core::FindTerminologyTerm(state_.app_state.sacm_package.value(), package_ref, term_ref);
    if (!term)
        return false;
    state_.terminology.selected_term_ref = term_ref;
    CopyTermToEditor(state_, *term);
    state_.terminology.editing_existing_term = true;
    state_.terminology.show_term_editor_modal = true;
    return true;
}

bool TerminologyActions::AddTermAsContextFromCanvas(const std::string& element_id,
                                                    const core::TerminologyPackageRef& package_ref,
                                                    const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before associating terminology.");
        return false;
    }

    core::TerminologyContextAssociationResult result = core::AssociateTerminologyTermWithElement(
        state_.app_state.sacm_package.value(), element_id, package_ref, term_ref);
    if (!result.success) {
        SetStatus(state_, "Could not associate term with element: " + result.error);
        return false;
    }

    if (!result.already_associated) {
        MarkTerminologyDocumentDirty(state_);
        state_.events.Emit(DocumentDirtyEvent{});
        if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                state_.app_state.active_project_file_path, state_.app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(state_, relative);
        }
    }
    SetStatus(state_,
              result.already_associated ? "Term is already associated with this element."
                                        : "Associated term with this element.");
    return true;
}

bool TerminologyActions::AddVisibleTermContextFromCanvas(const std::string& element_id,
                                                         const core::TerminologyPackageRef& package_ref,
                                                         const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before adding terminology context.");
        return false;
    }

    const std::string term_label = TermStatusLabel(state_.app_state.sacm_package.value(), package_ref, term_ref);
    core::TerminologyContextAssociationResult result = core::AddTerminologyTermAsVisibleContext(
        state_.app_state.sacm_package.value(), element_id, package_ref, term_ref);
    if (!result.success) {
        SetStatus(state_, "Could not add term as context: " + result.error);
        return false;
    }

    const bool parser_changed = SyncVisibleTerminologyContextToParser(state_.app_state, result);
    if (!result.already_associated) {
        MarkTerminologyDocumentDirty(state_);
        state_.events.Emit(DocumentDirtyEvent{});
        if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                state_.app_state.active_project_file_path, state_.app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(state_, relative);
        }
    }
    if (!result.already_associated || parser_changed)
        state_.events.Emit(TreeDirtyEvent{});
    SetStatus(state_,
              result.already_associated ? term_label + " is already attached as context to this element."
                                        : "Added " + term_label + " as context.");
    return true;
}

void TerminologyActions::SelectTerm(const core::TerminologyTermRef& term_ref) {
    state_.terminology.selected_term_ref = term_ref;
}

void TerminologyActions::BeginAddTerm() {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a terminology package before adding terms.");
        return;
    }
    ClearTermEditorBuffers(state_);
    state_.terminology.editing_existing_term = false;
    state_.terminology.show_term_editor_modal = true;
}

bool TerminologyActions::BeginEditTerm(const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value())
        return false;
    const sacm::Term* term = core::FindTerminologyTerm(
        state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return false;
    }
    state_.terminology.selected_term_ref = term_ref;
    CopyTermToEditor(state_, *term);
    state_.terminology.editing_existing_term = true;
    state_.terminology.show_term_editor_modal = true;
    return true;
}

bool TerminologyActions::ConfirmTermEdit() {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    const core::TerminologyTermDraft draft = TermDraftFromEditor(state_);
    std::string error;
    if (state_.terminology.editing_existing_term) {
        if (!core::UpdateTerminologyTerm(state_.app_state.sacm_package.value(),
                                         state_.terminology.selected_package_ref,
                                         state_.terminology.selected_term_ref,
                                         draft,
                                         error)) {
            SetStatus(state_, "Term update failed: " + error);
            return false;
        }
        SetStatus(state_, "Updated term " + draft.value + ".");
    } else {
        core::TerminologyTermCreateResult result = core::CreateTerminologyTerm(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, draft);
        if (!result.success) {
            SetStatus(state_, "Term create failed: " + result.error);
            return false;
        }
        state_.terminology.selected_term_ref = result.term_ref;
        SetStatus(state_, "Added term " + draft.value + ".");
    }

    MarkTerminologyDocumentDirty(state_);
    if (RefreshVisibleTerminologyContextProjection(state_.app_state))
        state_.events.Emit(TreeDirtyEvent{});
    state_.terminology.show_term_editor_modal = false;
    return true;
}

void TerminologyActions::BeginDeleteTerm(const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value())
        return;
    const sacm::Term* term = core::FindTerminologyTerm(
        state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return;
    }
    state_.terminology.selected_term_ref = term_ref;
    state_.terminology.pending_delete_term_usage_count =
        core::CountTerminologyTermUsage(state_.app_state.sacm_package.value(), *term);
    state_.terminology.show_delete_term_modal = true;
}

bool TerminologyActions::ConfirmDeleteTerm() {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    std::string error;
    if (!core::DeleteTerminologyTerm(state_.app_state.sacm_package.value(),
                                     state_.terminology.selected_package_ref,
                                     state_.terminology.selected_term_ref,
                                     error)) {
        SetStatus(state_, "Term delete failed: " + error);
        return false;
    }

    state_.terminology.selected_term_ref = core::TerminologyTermRef{};
    state_.terminology.show_delete_term_modal = false;
    MarkTerminologyDocumentDirty(state_);
    if (RefreshVisibleTerminologyContextProjection(state_.app_state))
        state_.events.Emit(TreeDirtyEvent{});
    SetStatus(state_, "Deleted term.");
    return true;
}

void TerminologyActions::SelectCategory(const core::TerminologyCategoryRef& category_ref) {
    state_.terminology.selected_category_ref = category_ref;
}

void TerminologyActions::SetCategoryFilter(const std::string& category_filter) {
    CopyToBuffer(
        state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), category_filter);
}

void TerminologyActions::BeginAddCategory() {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a terminology package before adding categories.");
        return;
    }
    ClearCategoryEditorBuffers(state_);
    state_.terminology.editing_existing_category = false;
    state_.terminology.show_category_editor_modal = true;
}

bool TerminologyActions::BeginEditCategory(const core::TerminologyCategoryRef& category_ref) {
    if (!state_.app_state.sacm_package.has_value())
        return false;
    const sacm::Category* category = core::FindTerminologyCategory(
        state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, category_ref);
    if (!category) {
        SetStatus(state_, "Category not found.");
        return false;
    }

    state_.terminology.selected_category_ref = category_ref;
    CopyCategoryToEditor(state_, *category);
    state_.terminology.editing_existing_category = true;
    state_.terminology.show_category_editor_modal = true;
    return true;
}

void TerminologyActions::ConfirmCategoryEdit() {
    if (!state_.app_state.sacm_package.has_value())
        return;

    const core::TerminologyCategoryDraft draft = CategoryDraftFromEditor(state_);
    std::string error;
    if (state_.terminology.editing_existing_category) {
        if (!core::UpdateTerminologyCategory(state_.app_state.sacm_package.value(),
                                             state_.terminology.selected_package_ref,
                                             state_.terminology.selected_category_ref,
                                             draft,
                                             error)) {
            SetStatus(state_, "Category update failed: " + error);
            return;
        }
        SetStatus(state_, "Updated category " + draft.name + ".");
    } else {
        core::TerminologyCategoryCreateResult result = core::CreateTerminologyCategory(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, draft);
        if (!result.success) {
            SetStatus(state_, "Category create failed: " + result.error);
            return;
        }
        state_.terminology.selected_category_ref = result.category_ref;
        SetStatus(state_, "Added category " + draft.name + ".");
    }

    MarkTerminologyDocumentDirty(state_);
    state_.terminology.show_category_editor_modal = false;
}

void TerminologyActions::BeginDeleteCategory(const core::TerminologyCategoryRef& category_ref) {
    if (!state_.app_state.sacm_package.has_value())
        return;
    const sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return;
    }
    const sacm::Category* category = core::FindTerminologyCategory(*terminology_package, category_ref);
    if (!category) {
        SetStatus(state_, "Category not found.");
        return;
    }

    state_.terminology.selected_category_ref = category_ref;
    state_.terminology.pending_delete_category_term_count =
        core::CountTermsUsingCategory(*terminology_package, category_ref);
    state_.terminology.show_delete_category_modal = true;
}

void TerminologyActions::ConfirmDeleteCategory() {
    if (!state_.app_state.sacm_package.has_value())
        return;

    std::string error;
    if (!core::DeleteTerminologyCategory(state_.app_state.sacm_package.value(),
                                         state_.terminology.selected_package_ref,
                                         state_.terminology.selected_category_ref,
                                         error)) {
        SetStatus(state_, "Category delete failed: " + error);
        return;
    }

    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.show_delete_category_modal = false;
    MarkTerminologyDocumentDirty(state_);
    SetStatus(state_, "Deleted category.");
}

void TerminologyActions::SeedRecommendedCategories() {
    if (!state_.app_state.sacm_package.has_value())
        return;

    sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return;
    }

    const char* recommended[] = {"Operational Context",
                                 "System",
                                 "Hazard / Risk",
                                 "Evidence",
                                 "Requirement",
                                 "Standard",
                                 "Project Specific",
                                 "Deprecated"};
    int added = 0;
    for (const char* name : recommended) {
        if (CategoryNameExists(*terminology_package, name))
            continue;
        core::TerminologyCategoryDraft draft;
        draft.name = name;
        core::TerminologyCategoryCreateResult result = core::CreateTerminologyCategory(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref, draft);
        if (result.success)
            ++added;
    }

    if (added > 0) {
        MarkTerminologyDocumentDirty(state_);
        SetStatus(state_, "Added recommended terminology categories.");
    } else {
        SetStatus(state_, "Recommended terminology categories already exist.");
    }
}

void TerminologyActions::BeginFindUsages(const core::TerminologyPackageRef& package_ref,
                                         const core::TerminologyTermRef& term_ref) {
    state_.terminology.usages_active = true;
    state_.terminology.focus_usages_tab = true;
    state_.terminology.usage_search_package_ref = package_ref;
    state_.terminology.usage_search_term_ref = term_ref;
    state_.terminology.usage_search_term_value.clear();
    state_.terminology.usage_search_term_name.clear();
    state_.terminology.usage_search_message.clear();
    state_.terminology.usage_search_error.clear();
    state_.terminology.usage_results.clear();
    state_.terminology.selected_usage_index = -1;

    if (!state_.app_state.sacm_package.has_value()) {
        state_.terminology.usage_search_error = "Open a SACM model before finding terminology usages.";
        SetStatus(state_, state_.terminology.usage_search_error);
        return;
    }

    core::TerminologyTermUsageSearchResult result =
        core::FindTerminologyTermUsages(state_.app_state.sacm_package.value(), package_ref, term_ref);
    state_.terminology.usage_search_term_value = result.term_value;
    state_.terminology.usage_search_term_name = result.term_name;
    if (!result.success) {
        state_.terminology.usage_search_error = result.error;
        SetStatus(state_, "Find usages failed: " + result.error);
        return;
    }

    state_.terminology.usage_results = std::move(result.usages);
    if (!state_.terminology.usage_results.empty())
        state_.terminology.selected_usage_index = 0;
    const int usage_count = static_cast<int>(state_.terminology.usage_results.size());
    const std::string label =
        state_.terminology.usage_search_term_value.empty() ? "term" : state_.terminology.usage_search_term_value;
    SetStatus(state_,
              "Found " + std::to_string(usage_count) + " usage" + (usage_count == 1 ? "" : "s") + " of " + label + ".");
}

void TerminologyActions::NavigateToUsage(std::size_t usage_index) {
    if (usage_index >= state_.terminology.usage_results.size())
        return;
    state_.terminology.selected_usage_index = static_cast<int>(usage_index);
    const core::TerminologyTermUsage& usage = state_.terminology.usage_results[usage_index];
    if (usage.element_id.empty()) {
        SetStatus(state_, "The selected usage has no navigable element id.");
        return;
    }
    state_.workbench.show_gsn_tab = true;
    state_.events.Emit(SelectionChangedEvent{usage.element_id, true});
    state_.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
}

void TerminologyActions::ChangeMeaningFromCanvas(const std::string& element_id, const std::string& term_value) {
    (void)element_id;
    (void)term_value;
    state_.events.Emit(ModalRequestEvent{ModalKind::NotImplemented, true, "Change linked terminology meaning"});
}

void TerminologyActions::BeginQuickDefineTerm(const std::string& element_id, const std::string& term_value) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before defining terms.");
        return;
    }

    const core::TerminologyPackageRef target_package_ref = ResolveQuickDefineTargetPackage(state_, element_id);
    if (!HasTerminologyPackageRef(target_package_ref)) {
        SetStatus(state_, "Create a TerminologyPackage before defining terms from text.");
        return;
    }

    const std::string trimmed_term = TrimWhitespace(term_value);
    ClearTermEditorBuffers(state_);
    CopyToBuffer(state_.terminology.term_value_buf, sizeof(state_.terminology.term_value_buf), trimmed_term);
    state_.terminology.quick_define_element_id = element_id;
    state_.terminology.quick_define_source_text = trimmed_term;
    state_.terminology.quick_define_target_package_ref = target_package_ref;
    state_.terminology.show_quick_define_term_modal = true;
}

void TerminologyActions::BeginLinkExistingTerm(const std::string& element_id, const std::string& term_value) {
    if (state_.app_state.sacm_package.has_value()) {
        const core::TerminologyPackageRef target_package_ref = ResolveQuickDefineTargetPackage(state_, element_id);
        if (HasTerminologyPackageRef(target_package_ref)) {
            state_.terminology.selected_package_ref = target_package_ref;
            if (const sacm::TerminologyPackage* package =
                    core::FindTerminologyPackage(state_.app_state.sacm_package.value(), target_package_ref)) {
                CopyTerminologyPackageToEditor(state_, *package);
            }
        }
    }

    const std::string trimmed_term = TrimWhitespace(term_value);
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), trimmed_term);
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_,
              "Filtered the glossary for " + trimmed_term +
                  ". Select a term to link when occurrence binding is available.");
}

bool TerminologyActions::ConfirmQuickDefineTerm(bool add_as_context) {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    const core::TerminologyTermDraft draft = TermDraftFromEditor(state_);
    core::TerminologyTermCreateResult result = core::CreateTerminologyTerm(
        state_.app_state.sacm_package.value(), state_.terminology.quick_define_target_package_ref, draft);
    if (!result.success) {
        SetStatus(state_, "Term create failed: " + result.error);
        return false;
    }

    state_.terminology.selected_package_ref = state_.terminology.quick_define_target_package_ref;
    state_.terminology.selected_term_ref = result.term_ref;
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), draft.value);
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package = core::FindTerminologyPackage(
            state_.app_state.sacm_package.value(), state_.terminology.selected_package_ref)) {
        CopyTerminologyPackageToEditor(state_, *package);
    }

    MarkTerminologyDocumentDirty(state_);
    state_.events.Emit(TreeDirtyEvent{});
    state_.events.Emit(DocumentDirtyEvent{});
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    const std::string context_element_id = state_.terminology.quick_define_element_id;
    if (add_as_context)
        AddVisibleTermContextFromCanvas(
            context_element_id, state_.terminology.quick_define_target_package_ref, result.term_ref);

    state_.terminology.show_quick_define_term_modal = false;
    state_.terminology.quick_define_element_id.clear();
    state_.terminology.quick_define_source_text.clear();
    if (!add_as_context)
        SetStatus(state_, "Added term " + draft.value + ".");
    return true;
}

void TerminologyActions::HandleProblemQuickFix(const core::ProblemItem& problem) {
    if (problem.type == "TerminologyUndefinedAcronym") {
        BeginQuickDefineTerm(problem.element_id, problem.quick_fix_payload);
        return;
    }
    if (problem.type == "TerminologyAmbiguity") {
        BeginLinkExistingTerm(problem.element_id, problem.quick_fix_payload);
        return;
    }
    if (StartsWith(problem.type, "TerminologyTerm")) {
        TerminologyTermQuickFixPayload payload;
        if (!DecodeTerminologyTermQuickFixPayload(problem.quick_fix_payload, payload)) {
            SetStatus(state_, "Could not decode terminology quick fix target.");
            return;
        }
        if (!OpenTerminologyProblemTerm(state_, payload.package_ref, payload.term_ref, payload.term_value)) {
            SetStatus(state_, "Terminology quick fix target was not found.");
            return;
        }
        if (problem.type == "TerminologyTermDuplicateDefinition") {
            SetStatus(state_, "Filtered glossary to duplicated term definition " + payload.term_value + ".");
            return;
        }
        BeginEditTerm(payload.term_ref);
        SetStatus(state_, "Opened terminology term editor.");
        return;
    }
    if (!problem.element_id.empty()) {
        state_.events.Emit(SelectionChangedEvent{problem.element_id, true});
        state_.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
    }
}

void TerminologyActions::IgnoreSuggestion(const std::string& element_id, const std::string& term_value) {
    const std::string trimmed_term = TrimWhitespace(term_value);
    state_.terminology.ignored_suggestion_keys.insert(TerminologySuggestionKey(element_id, trimmed_term));
    SetStatus(state_, "Ignored terminology suggestion " + trimmed_term + " for this session.");
}

bool TerminologyActions::IsSuggestionIgnored(const std::string& element_id, const std::string& term_value) const {
    const std::string key = TerminologySuggestionKey(element_id, TrimWhitespace(term_value));
    return state_.terminology.ignored_suggestion_keys.count(key) > 0;
}

} // namespace app::actions
