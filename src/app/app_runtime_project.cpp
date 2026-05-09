#include "app/app_runtime.h"
#include "app/app_runtime_state.h"
#include "app/native_file_dialogs.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "core/project_service.h"
#include "core/reviews/review_item.h"
#include "core/terminology_package_service.h"
#include "imgui.h"
#include "sacm/sacm_package_tree.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace app {
namespace {

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

void ClearProposalHighlightState(ui::UiState& ui_state) {
    ui_state.proposal_highlight_ids.clear();
    ui_state.marked_for_removal.clear();
    ui_state.center_on_marked = false;
    ui_state.dim_non_proposal_nodes = false;
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

void InvalidateSacmPackageTreeCache(AppRuntimeState& state, const std::filesystem::path& relative_path) {
    state.sacm_package_tree_cache.erase(relative_path.generic_string());
}

void CopyTerminologyPackageToEditor(AppRuntimeState& state, const sacm::TerminologyPackage& package) {
    CopyToBuffer(state.terminology_package_name_buf, sizeof(state.terminology_package_name_buf), package.name);
    CopyToBuffer(state.terminology_package_description_buf,
                 sizeof(state.terminology_package_description_buf),
                 package.description);
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

void ClearTermEditorBuffers(AppRuntimeState& state) {
    CopyToBuffer(state.term_value_buf, sizeof(state.term_value_buf), "");
    CopyToBuffer(state.term_name_buf, sizeof(state.term_name_buf), "");
    CopyToBuffer(state.term_definition_buf, sizeof(state.term_definition_buf), "");
    CopyToBuffer(state.term_categories_buf, sizeof(state.term_categories_buf), "");
    CopyToBuffer(state.term_external_reference_buf, sizeof(state.term_external_reference_buf), "");
    CopyToBuffer(state.term_origin_buf, sizeof(state.term_origin_buf), "");
}

void CopyTermToEditor(AppRuntimeState& state, const sacm::Term& term) {
    CopyToBuffer(state.term_value_buf, sizeof(state.term_value_buf), term.value);
    CopyToBuffer(state.term_name_buf, sizeof(state.term_name_buf), term.name);
    CopyToBuffer(state.term_definition_buf, sizeof(state.term_definition_buf), term.description);
    CopyToBuffer(state.term_categories_buf, sizeof(state.term_categories_buf), JoinCategoryRefs(term.category_refs));
    CopyToBuffer(state.term_external_reference_buf, sizeof(state.term_external_reference_buf), term.externalReference);
    CopyToBuffer(state.term_origin_buf, sizeof(state.term_origin_buf), term.origin);
}

void ClearCategoryEditorBuffers(AppRuntimeState& state) {
    CopyToBuffer(state.category_name_buf, sizeof(state.category_name_buf), "");
    CopyToBuffer(state.category_description_buf, sizeof(state.category_description_buf), "");
}

void CopyCategoryToEditor(AppRuntimeState& state, const sacm::Category& category) {
    CopyToBuffer(state.category_name_buf, sizeof(state.category_name_buf), category.name);
    CopyToBuffer(state.category_description_buf, sizeof(state.category_description_buf), category.description);
}

core::TerminologyTermDraft TermDraftFromEditor(const AppRuntimeState& state) {
    core::TerminologyTermDraft draft;
    draft.value = TrimWhitespace(state.term_value_buf);
    draft.name = TrimWhitespace(state.term_name_buf);
    draft.description = TrimWhitespace(state.term_definition_buf);
    draft.category_refs = SplitCategoryRefs(state.term_categories_buf);
    draft.externalReference = TrimWhitespace(state.term_external_reference_buf);
    draft.origin = TrimWhitespace(state.term_origin_buf);
    return draft;
}

core::TerminologyCategoryDraft CategoryDraftFromEditor(const AppRuntimeState& state) {
    core::TerminologyCategoryDraft draft;
    draft.name = TrimWhitespace(state.category_name_buf);
    draft.description = TrimWhitespace(state.category_description_buf);
    return draft;
}

bool CategoryNameExists(const sacm::TerminologyPackage& package, const std::string& name) {
    return std::any_of(package.categories.begin(), package.categories.end(), [&](const sacm::Category& category) {
        return category.name == name;
    });
}

void MarkTerminologyDocumentDirty(AppRuntimeState& state) {
    state.app_state.mark_dirty();
    state.document_dirty = true;
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
    if (HasTerminologyPackageRef(state.selected_terminology_package_ref) &&
        core::FindTerminologyPackage(package, state.selected_terminology_package_ref)) {
        if (IsAssuranceCaseTerminologyPackage(package, state.selected_terminology_package_ref) ||
            (containing_argument_package &&
             IsArgumentTerminologyPackage(*containing_argument_package, state.selected_terminology_package_ref))) {
            return state.selected_terminology_package_ref;
        }
    }

    if (containing_argument_package && !containing_argument_package->terminologyPackages.empty())
        return TerminologyPackageRefFor(containing_argument_package->terminologyPackages.front());
    if (!package.terminologyPackages.empty())
        return TerminologyPackageRefFor(package.terminologyPackages.front());
    return {};
}

std::string TerminologySuggestionKey(const std::string& element_id, const std::string& term_value) {
    return element_id + "\n" + term_value;
}

std::string FirstElementIdForArgumentPackage(const sacm::AssuranceCasePackage& package,
                                             const sacm::SacmPackageTreeNode& selected_package) {
    for (const auto& argument_package : package.argumentPackages) {
        const bool id_matches = !selected_package.id.empty() && argument_package.id == selected_package.id;
        const bool gid_matches = !selected_package.gid.empty() && argument_package.gid == selected_package.gid;
        if (!id_matches && !gid_matches)
            continue;
        if (!argument_package.claims.empty())
            return argument_package.claims.front().id;
        if (!argument_package.argumentReasonings.empty())
            return argument_package.argumentReasonings.front().id;
        if (!argument_package.artifactReferences.empty())
            return argument_package.artifactReferences.front().id;
    }
    return {};
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

bool ParserModelHasElement(const parser::AssuranceCase& model, const std::string& element_id) {
    return std::any_of(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == element_id || element.name == element_id;
    });
}

parser::SacmElement* FindParserElement(parser::AssuranceCase& model, const std::string& id, const std::string& gid) {
    for (parser::SacmElement& element : model.elements) {
        if ((!id.empty() && element.id == id) || (!gid.empty() && element.id == gid))
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
    if (!ParserModelHasElement(model, artifact_reference->id)) {
        parser::SacmElement element;
        element.id = artifact_reference->id;
        element.name = artifact_reference->name;
        element.type = "artifactreference";
        element.description = artifact_reference->description;
        element.name_langs = artifact_reference->name_ml.texts;
        element.description_langs = artifact_reference->description_ml.texts;
        model.elements.push_back(std::move(element));
        changed = true;
    }
    if (!ParserModelHasElement(model, context->id)) {
        parser::SacmElement element;
        element.id = context->id;
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

} // namespace

void AppRuntime::BeginCreateProject() {
    std::string selected_path;
    std::string error_message;
    const dialogs::DialogResult result = dialogs::BrowseForProjectParentFolder(
        impl_->project_controller->project_parent_buf, selected_path, error_message);
    if (result == dialogs::DialogResult::Selected) {
        CopyToBuffer(impl_->project_controller->project_parent_buf,
                     sizeof(impl_->project_controller->project_parent_buf),
                     selected_path);
        if (impl_->project_controller->project_name_buf[0] == '\0') {
            CopyToBuffer(impl_->project_controller->project_name_buf,
                         sizeof(impl_->project_controller->project_name_buf),
                         "MySafetyCase");
        }
        impl_->project_controller->show_create_project_modal = true;
    } else if (result == dialogs::DialogResult::Failed) {
        SetStatus("Browse failed: " + error_message);
    }
}

void AppRuntime::BeginOpenProject() {
    std::string default_path = impl_->project_controller->open_project_path_buf;
    if (default_path.empty() && !impl_->project_controller->recent_projects.empty()) {
        default_path = impl_->project_controller->recent_projects.front().path;
    }

    std::string selected_path;
    std::string error_message;
    const dialogs::DialogResult result = dialogs::BrowseForProjectManifest(default_path, selected_path, error_message);
    if (result == dialogs::DialogResult::Selected) {
        CopyToBuffer(impl_->project_controller->open_project_path_buf,
                     sizeof(impl_->project_controller->open_project_path_buf),
                     selected_path);
        TryOpenProjectManifest(selected_path);
    } else if (result == dialogs::DialogResult::Failed) {
        SetStatus("Browse failed: " + error_message);
    }
}

void AppRuntime::TouchCurrentProjectRecent() {
    impl_->project_controller->TouchCurrentProjectRecent(impl_->app_state);
}

void AppRuntime::BeginCreateProjectSacmFile() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::Sacm, "main.sacm");
}

void AppRuntime::BeginCreateProjectEvidenceRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::EvidenceRegister,
                                                      "evidence-register.af.json");
}

void AppRuntime::BeginCreateProjectJ3377CaeRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::J3377CaeRegister,
                                                      "j3377-cae-register.af.json");
}

void AppRuntime::OpenProjectFile(const core::ProjectFileEntry& entry) {
    if (!impl_->app_state.open_project_file(entry))
        return;

    ui::UiState& ui_state = ui::GetUiState();
    if (entry.role == core::ProjectFileRole::SacmArgument) {
        impl_->proposal_controller->ClearActiveState();
        ClearProposalHighlightState(ui_state);
        impl_->document_dirty = false;
        impl_->tree_needs_rebuild = true;
        impl_->pending_focus_root = true;
        impl_->show_gsn_tab = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::EvidenceRegister) {
        impl_->show_evidence_tab = true;
        ui_state.center_view = ui::CenterView::EvidenceRegister;
        impl_->force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::J3377CaeRegister) {
        impl_->show_cse_tab = true;
        ui_state.center_view = ui::CenterView::CseRegister;
        impl_->force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::ReviewProposal) {
        std::string proposal_id = entry.relativePath.filename().generic_string();
        const std::string suffix = ".afpatch.json";
        if (proposal_id.size() >= suffix.size() &&
            proposal_id.compare(proposal_id.size() - suffix.size(), suffix.size(), suffix) == 0) {
            proposal_id.erase(proposal_id.size() - suffix.size());
        }
        PreviewProposalById(proposal_id);
    }
}

void AppRuntime::OpenProjectPackageNode(const core::ProjectFileEntry& entry, const sacm::SacmPackageTreeNode& node) {
    ui::UiState& ui_state = ui::GetUiState();
    impl_->selected_package_node = node;
    impl_->selected_package_file_path = impl_->app_state.current_project.has_value()
                                            ? impl_->app_state.current_project->rootPath / entry.relativePath
                                            : entry.relativePath;

    if (node.type == sacm::SacmPackageNodeType::ArgumentPackage) {
        if (!CanSwitchProjectSacmFile(impl_->app_state, entry)) {
            SetStatus("Save the current SACM file before opening another package.");
            return;
        }
        if (!EnsureProjectSacmFileOpen(*impl_, entry, true))
            return;

        impl_->proposal_controller->ClearActiveState();
        ClearProposalHighlightState(ui_state);
        impl_->document_dirty = false;
        impl_->tree_needs_rebuild = true;
        impl_->pending_focus_root = false;
        impl_->show_gsn_tab = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->force_center_tab_selection = true;

        if (impl_->app_state.sacm_package.has_value()) {
            std::string first_id = FirstElementIdForArgumentPackage(impl_->app_state.sacm_package.value(), node);
            if (!first_id.empty()) {
                ui_state.selected_element_id = first_id;
                ui_state.center_on_selection = true;
            } else {
                SetStatus("Opened argument package; no focusable argument element was found in the package.");
            }
        }
        return;
    }

    if (node.type == sacm::SacmPackageNodeType::TerminologyPackage) {
        if (!CanSwitchProjectSacmFile(impl_->app_state, entry)) {
            SetStatus("Save the current SACM file before opening another package.");
            return;
        }
        if (!EnsureProjectSacmFileOpen(*impl_, entry, false))
            return;
        if (!impl_->app_state.sacm_package.has_value()) {
            SetStatus("Opened SACM file, but no editable package model was available.");
            return;
        }

        core::TerminologyPackageRef package_ref{node.id, node.gid};
        const sacm::TerminologyPackage* terminology_package =
            core::FindTerminologyPackage(impl_->app_state.sacm_package.value(), package_ref);
        if (!terminology_package) {
            SetStatus("Terminology package was not found in the editable model.");
            return;
        }

        impl_->selected_terminology_package_ref = package_ref;
        impl_->selected_terminology_package_file_path = impl_->selected_package_file_path;
        impl_->selected_terminology_term_ref = core::TerminologyTermRef{};
        impl_->selected_terminology_category_ref = core::TerminologyCategoryRef{};
        CopyToBuffer(impl_->terminology_category_filter_buf, sizeof(impl_->terminology_category_filter_buf), "");
        CopyTerminologyPackageToEditor(*impl_, *terminology_package);
        impl_->show_terminology_package_tab = true;
        ui_state.center_view = ui::CenterView::TerminologyPackage;
        impl_->force_center_tab_selection = true;
        return;
    }

    impl_->show_package_details_tab = true;
    ui_state.center_view = ui::CenterView::PackageDetails;
    impl_->force_center_tab_selection = true;
}

void AppRuntime::BeginAddTerminologyPackage(const core::ProjectFileEntry& entry,
                                            const sacm::SacmPackageTreeNode& parent_node) {
    if (parent_node.type != sacm::SacmPackageNodeType::AssuranceCasePackage)
        return;
    if (!CanSwitchProjectSacmFile(impl_->app_state, entry)) {
        SetStatus("Save the current SACM file before adding a terminology package.");
        return;
    }

    impl_->pending_terminology_package_parent_entry = entry;
    CopyToBuffer(impl_->new_terminology_package_name_buf,
                 sizeof(impl_->new_terminology_package_name_buf),
                 "Terminology Package");
    CopyToBuffer(
        impl_->new_terminology_package_description_buf, sizeof(impl_->new_terminology_package_description_buf), "");
    impl_->show_create_terminology_package_modal = true;
}

void AppRuntime::ConfirmAddTerminologyPackage() {
    if (!impl_->pending_terminology_package_parent_entry.has_value()) {
        impl_->show_create_terminology_package_modal = false;
        return;
    }

    const core::ProjectFileEntry entry = impl_->pending_terminology_package_parent_entry.value();
    if (!CanSwitchProjectSacmFile(impl_->app_state, entry)) {
        SetStatus("Save the current SACM file before adding a terminology package.");
        return;
    }
    if (!EnsureProjectSacmFileOpen(*impl_, entry, false))
        return;
    if (!impl_->app_state.sacm_package.has_value()) {
        SetStatus("Could not load an editable SACM package model.");
        return;
    }

    core::TerminologyPackageCreateResult result =
        core::CreateTerminologyPackage(impl_->app_state.sacm_package.value(),
                                       TrimWhitespace(impl_->new_terminology_package_name_buf),
                                       TrimWhitespace(impl_->new_terminology_package_description_buf));
    if (!result.success) {
        SetStatus("Terminology package create failed: " + result.error);
        return;
    }

    impl_->selected_terminology_package_ref = result.package_ref;
    impl_->selected_terminology_term_ref = core::TerminologyTermRef{};
    impl_->selected_terminology_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(impl_->terminology_category_filter_buf, sizeof(impl_->terminology_category_filter_buf), "");
    impl_->selected_terminology_package_file_path = impl_->app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package =
            core::FindTerminologyPackage(impl_->app_state.sacm_package.value(), result.package_ref)) {
        CopyTerminologyPackageToEditor(*impl_, *package);
    }
    MarkTerminologyDocumentDirty(*impl_);
    SyncTerminologyProblems();
    InvalidateSacmPackageTreeCache(*impl_, entry.relativePath);
    impl_->show_create_terminology_package_modal = false;
    impl_->pending_terminology_package_parent_entry.reset();
    impl_->show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    impl_->force_center_tab_selection = true;
    SetStatus("Added terminology package " + result.package_ref.id + ".");
}

void AppRuntime::ApplyTerminologyPackageEdits() {
    if (!impl_->app_state.sacm_package.has_value())
        return;

    std::string error;
    if (!core::UpdateTerminologyPackage(impl_->app_state.sacm_package.value(),
                                        impl_->selected_terminology_package_ref,
                                        TrimWhitespace(impl_->terminology_package_name_buf),
                                        TrimWhitespace(impl_->terminology_package_description_buf),
                                        error)) {
        SetStatus("Terminology package update failed: " + error);
        return;
    }

    MarkTerminologyDocumentDirty(*impl_);
    SyncTerminologyProblems();
    if (impl_->app_state.current_project.has_value() && !impl_->app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(impl_->app_state.active_project_file_path,
                                                                         impl_->app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(*impl_, relative);
    }
}

void AppRuntime::BeginDeleteTerminologyPackage() {
    impl_->show_delete_terminology_package_modal = true;
}

void AppRuntime::ConfirmDeleteTerminologyPackage() {
    if (!impl_->app_state.sacm_package.has_value())
        return;

    std::string error;
    if (!core::DeleteTerminologyPackage(
            impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref, error)) {
        SetStatus("Terminology package delete failed: " + error);
        return;
    }

    MarkTerminologyDocumentDirty(*impl_);
    SyncTerminologyProblems();
    if (impl_->app_state.current_project.has_value() && !impl_->app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(impl_->app_state.active_project_file_path,
                                                                         impl_->app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(*impl_, relative);
    }
    impl_->selected_terminology_package_ref = core::TerminologyPackageRef{};
    impl_->show_delete_terminology_package_modal = false;
    impl_->show_terminology_package_tab = false;
    ui::GetUiState().center_view = ui::CenterView::PackageDetails;
    impl_->force_center_tab_selection = true;
    SetStatus("Deleted terminology package.");
}

void AppRuntime::SelectTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    impl_->selected_terminology_term_ref = term_ref;
}

void AppRuntime::BeginAddTerminologyTerm() {
    if (!impl_->app_state.sacm_package.has_value()) {
        SetStatus("Open a terminology package before adding terms.");
        return;
    }
    ClearTermEditorBuffers(*impl_);
    impl_->editing_existing_terminology_term = false;
    impl_->show_terminology_term_editor_modal = true;
}

void AppRuntime::BeginEditTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    if (!impl_->app_state.sacm_package.has_value())
        return;
    const sacm::Term* term = core::FindTerminologyTerm(
        impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref, term_ref);
    if (!term) {
        SetStatus("Term not found.");
        return;
    }
    impl_->selected_terminology_term_ref = term_ref;
    CopyTermToEditor(*impl_, *term);
    impl_->editing_existing_terminology_term = true;
    impl_->show_terminology_term_editor_modal = true;
}

void AppRuntime::ConfirmTerminologyTermEdit() {
    if (!impl_->app_state.sacm_package.has_value())
        return;

    const core::TerminologyTermDraft draft = TermDraftFromEditor(*impl_);
    std::string error;
    if (impl_->editing_existing_terminology_term) {
        if (!core::UpdateTerminologyTerm(impl_->app_state.sacm_package.value(),
                                         impl_->selected_terminology_package_ref,
                                         impl_->selected_terminology_term_ref,
                                         draft,
                                         error)) {
            SetStatus("Term update failed: " + error);
            return;
        }
        SetStatus("Updated term " + draft.value + ".");
    } else {
        core::TerminologyTermCreateResult result = core::CreateTerminologyTerm(
            impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref, draft);
        if (!result.success) {
            SetStatus("Term create failed: " + result.error);
            return;
        }
        impl_->selected_terminology_term_ref = result.term_ref;
        SetStatus("Added term " + draft.value + ".");
    }

    MarkTerminologyDocumentDirty(*impl_);
    if (RefreshVisibleTerminologyContextProjection(impl_->app_state))
        impl_->events.Emit(TreeDirtyEvent{});
    SyncTerminologyProblems();
    impl_->show_terminology_term_editor_modal = false;
}

void AppRuntime::OpenTerminologyTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                               const core::TerminologyTermRef& term_ref) {
    if (!impl_->app_state.sacm_package.has_value()) {
        SetStatus("Open a SACM model before opening terminology terms.");
        return;
    }

    const sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(impl_->app_state.sacm_package.value(), package_ref);
    if (!terminology_package) {
        SetStatus("Terminology package not found.");
        return;
    }
    const sacm::Term* term = core::FindTerminologyTerm(*terminology_package, term_ref);
    if (!term) {
        SetStatus("Term not found.");
        return;
    }

    impl_->selected_terminology_package_ref = package_ref;
    impl_->selected_terminology_term_ref = term_ref;
    impl_->selected_terminology_category_ref = core::TerminologyCategoryRef{};
    impl_->selected_terminology_package_file_path = impl_->app_state.active_project_file_path;
    CopyTerminologyPackageToEditor(*impl_, *terminology_package);
    CopyToBuffer(impl_->terminology_filter_buf, sizeof(impl_->terminology_filter_buf), term->value);
    CopyToBuffer(impl_->terminology_category_filter_buf, sizeof(impl_->terminology_category_filter_buf), "");
    impl_->show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    impl_->force_center_tab_selection = true;
    SetStatus("Opened term " + term->value + ".");
}

void AppRuntime::EditTerminologyTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                               const core::TerminologyTermRef& term_ref) {
    OpenTerminologyTermFromCanvas(package_ref, term_ref);
    if (!impl_->app_state.sacm_package.has_value())
        return;
    if (!core::FindTerminologyTerm(impl_->app_state.sacm_package.value(), package_ref, term_ref))
        return;
    BeginEditTerminologyTerm(term_ref);
}

void AppRuntime::AddTerminologyTermAsContextFromCanvas(const std::string& element_id,
                                                       const core::TerminologyPackageRef& package_ref,
                                                       const core::TerminologyTermRef& term_ref) {
    if (!impl_->app_state.sacm_package.has_value()) {
        SetStatus("Open a SACM model before associating terminology.");
        return;
    }

    core::TerminologyContextAssociationResult result = core::AssociateTerminologyTermWithElement(
        impl_->app_state.sacm_package.value(), element_id, package_ref, term_ref);
    if (!result.success) {
        SetStatus("Could not associate term with element: " + result.error);
        return;
    }

    if (!result.already_associated) {
        MarkTerminologyDocumentDirty(*impl_);
        impl_->events.Emit(DocumentDirtyEvent{});
        if (impl_->app_state.current_project.has_value() && !impl_->app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                impl_->app_state.active_project_file_path, impl_->app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(*impl_, relative);
        }
    }
    SyncTerminologyProblems();
    SetStatus(result.already_associated ? "Term is already associated with this element."
                                        : "Associated term with this element.");
}

void AppRuntime::AddVisibleTerminologyTermContextFromCanvas(const std::string& element_id,
                                                            const core::TerminologyPackageRef& package_ref,
                                                            const core::TerminologyTermRef& term_ref) {
    if (!impl_->app_state.sacm_package.has_value()) {
        SetStatus("Open a SACM model before adding terminology context.");
        return;
    }

    const std::string term_label = TermStatusLabel(impl_->app_state.sacm_package.value(), package_ref, term_ref);
    core::TerminologyContextAssociationResult result = core::AddTerminologyTermAsVisibleContext(
        impl_->app_state.sacm_package.value(), element_id, package_ref, term_ref);
    if (!result.success) {
        SetStatus("Could not add term as context: " + result.error);
        return;
    }

    const bool parser_changed = SyncVisibleTerminologyContextToParser(impl_->app_state, result);
    if (!result.already_associated) {
        MarkTerminologyDocumentDirty(*impl_);
        impl_->events.Emit(DocumentDirtyEvent{});
        if (impl_->app_state.current_project.has_value() && !impl_->app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                impl_->app_state.active_project_file_path, impl_->app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(*impl_, relative);
        }
    }
    if (!result.already_associated || parser_changed)
        impl_->events.Emit(TreeDirtyEvent{});
    SyncTerminologyProblems();
    SetStatus(result.already_associated ? term_label + " is already attached as context to this element."
                                        : "Added " + term_label + " as context.");
}

void AppRuntime::FindTerminologyUsagesFromCanvas(const core::TerminologyPackageRef& package_ref,
                                                 const core::TerminologyTermRef& term_ref) {
    BeginFindTerminologyUsages(package_ref, term_ref);
}

void AppRuntime::BeginFindTerminologyUsages(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    impl_->terminology_usages_active = true;
    impl_->focus_terminology_usages_tab = true;
    impl_->usage_search_package_ref = package_ref;
    impl_->usage_search_term_ref = term_ref;
    impl_->usage_search_term_value.clear();
    impl_->usage_search_term_name.clear();
    impl_->usage_search_message.clear();
    impl_->usage_search_error.clear();
    impl_->terminology_usage_results.clear();
    impl_->selected_terminology_usage_index = -1;

    if (!impl_->app_state.sacm_package.has_value()) {
        impl_->usage_search_error = "Open a SACM model before finding terminology usages.";
        SetStatus(impl_->usage_search_error);
        return;
    }

    core::TerminologyTermUsageSearchResult result =
        core::FindTerminologyTermUsages(impl_->app_state.sacm_package.value(), package_ref, term_ref);
    impl_->usage_search_term_value = result.term_value;
    impl_->usage_search_term_name = result.term_name;
    if (!result.success) {
        impl_->usage_search_error = result.error;
        SetStatus("Find usages failed: " + result.error);
        return;
    }

    impl_->terminology_usage_results = std::move(result.usages);
    if (!impl_->terminology_usage_results.empty())
        impl_->selected_terminology_usage_index = 0;
    const int usage_count = static_cast<int>(impl_->terminology_usage_results.size());
    const std::string label = impl_->usage_search_term_value.empty() ? "term" : impl_->usage_search_term_value;
    impl_->usage_search_message = std::to_string(usage_count) + " usage" + (usage_count == 1 ? "" : "s") + " found.";
    SetStatus("Found " + std::to_string(usage_count) + " usage" + (usage_count == 1 ? "" : "s") + " of " + label + ".");
}

void AppRuntime::NavigateToTerminologyUsage(std::size_t usage_index) {
    if (usage_index >= impl_->terminology_usage_results.size())
        return;
    impl_->selected_terminology_usage_index = static_cast<int>(usage_index);
    const core::TerminologyTermUsage& usage = impl_->terminology_usage_results[usage_index];
    if (usage.element_id.empty()) {
        SetStatus("The selected usage has no navigable element id.");
        return;
    }
    impl_->events.Emit(SelectionChangedEvent{usage.element_id, true});
    impl_->events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
}

void AppRuntime::ChangeTerminologyMeaningFromCanvas(const std::string& element_id, const std::string& term_value) {
    (void)element_id;
    (void)term_value;
    ShowNotImplementedModal("Change linked terminology meaning");
}

void AppRuntime::BeginQuickDefineTerminologyTerm(const std::string& element_id, const std::string& term_value) {
    if (!impl_->app_state.sacm_package.has_value()) {
        SetStatus("Open a SACM model before defining terms.");
        return;
    }

    const core::TerminologyPackageRef target_package_ref = ResolveQuickDefineTargetPackage(*impl_, element_id);
    if (!HasTerminologyPackageRef(target_package_ref)) {
        SetStatus("Create a TerminologyPackage before defining terms from text.");
        return;
    }

    ClearTermEditorBuffers(*impl_);
    CopyToBuffer(impl_->term_value_buf, sizeof(impl_->term_value_buf), TrimWhitespace(term_value));
    impl_->quick_define_element_id = element_id;
    impl_->quick_define_source_text = TrimWhitespace(term_value);
    impl_->quick_define_target_package_ref = target_package_ref;
    impl_->show_quick_define_term_modal = true;
}

void AppRuntime::BeginLinkExistingTerminologyTerm(const std::string& element_id, const std::string& term_value) {
    if (impl_->app_state.sacm_package.has_value()) {
        const core::TerminologyPackageRef target_package_ref = ResolveQuickDefineTargetPackage(*impl_, element_id);
        if (HasTerminologyPackageRef(target_package_ref)) {
            impl_->selected_terminology_package_ref = target_package_ref;
            if (const sacm::TerminologyPackage* package =
                    core::FindTerminologyPackage(impl_->app_state.sacm_package.value(), target_package_ref)) {
                CopyTerminologyPackageToEditor(*impl_, *package);
            }
        }
    }
    CopyToBuffer(impl_->terminology_filter_buf, sizeof(impl_->terminology_filter_buf), TrimWhitespace(term_value));
    impl_->show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    impl_->force_center_tab_selection = true;
    SetStatus("Filtered the glossary for " + TrimWhitespace(term_value) +
              ". Select a term to link when occurrence binding is available.");
}

void AppRuntime::IgnoreTerminologySuggestion(const std::string& element_id, const std::string& term_value) {
    impl_->ignored_terminology_suggestion_keys.insert(TerminologySuggestionKey(element_id, TrimWhitespace(term_value)));
    SetStatus("Ignored terminology suggestion " + TrimWhitespace(term_value) + " for this session.");
}

bool AppRuntime::IsTerminologySuggestionIgnored(const std::string& element_id, const std::string& term_value) const {
    const std::string key = TerminologySuggestionKey(element_id, TrimWhitespace(term_value));
    return impl_->ignored_terminology_suggestion_keys.count(key) > 0;
}

void AppRuntime::ConfirmQuickDefineTerminologyTerm(bool add_as_context) {
    if (!impl_->app_state.sacm_package.has_value())
        return;

    const core::TerminologyTermDraft draft = TermDraftFromEditor(*impl_);
    core::TerminologyTermCreateResult result = core::CreateTerminologyTerm(
        impl_->app_state.sacm_package.value(), impl_->quick_define_target_package_ref, draft);
    if (!result.success) {
        SetStatus("Term create failed: " + result.error);
        return;
    }

    impl_->selected_terminology_package_ref = impl_->quick_define_target_package_ref;
    impl_->selected_terminology_term_ref = result.term_ref;
    impl_->selected_terminology_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(impl_->terminology_filter_buf, sizeof(impl_->terminology_filter_buf), draft.value);
    CopyToBuffer(impl_->terminology_category_filter_buf, sizeof(impl_->terminology_category_filter_buf), "");
    impl_->selected_terminology_package_file_path = impl_->app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package = core::FindTerminologyPackage(
            impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref)) {
        CopyTerminologyPackageToEditor(*impl_, *package);
    }

    MarkTerminologyDocumentDirty(*impl_);
    SyncTerminologyProblems();
    impl_->events.Emit(TreeDirtyEvent{});
    impl_->events.Emit(DocumentDirtyEvent{});
    if (impl_->app_state.current_project.has_value() && !impl_->app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(impl_->app_state.active_project_file_path,
                                                                         impl_->app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(*impl_, relative);
    }
    const std::string context_element_id = impl_->quick_define_element_id;
    if (add_as_context)
        AddVisibleTerminologyTermContextFromCanvas(
            context_element_id, impl_->quick_define_target_package_ref, result.term_ref);

    impl_->show_quick_define_term_modal = false;
    impl_->quick_define_element_id.clear();
    impl_->quick_define_source_text.clear();
    if (!add_as_context)
        SetStatus("Added term " + draft.value + ".");
}

void AppRuntime::BeginDeleteTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    if (!impl_->app_state.sacm_package.has_value())
        return;
    const sacm::Term* term = core::FindTerminologyTerm(
        impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref, term_ref);
    if (!term) {
        SetStatus("Term not found.");
        return;
    }
    impl_->selected_terminology_term_ref = term_ref;
    impl_->pending_delete_terminology_term_usage_count =
        core::CountTerminologyTermUsage(impl_->app_state.sacm_package.value(), *term);
    impl_->show_delete_terminology_term_modal = true;
}

void AppRuntime::ConfirmDeleteTerminologyTerm() {
    if (!impl_->app_state.sacm_package.has_value())
        return;

    std::string error;
    if (!core::DeleteTerminologyTerm(impl_->app_state.sacm_package.value(),
                                     impl_->selected_terminology_package_ref,
                                     impl_->selected_terminology_term_ref,
                                     error)) {
        SetStatus("Term delete failed: " + error);
        return;
    }

    impl_->selected_terminology_term_ref = core::TerminologyTermRef{};
    impl_->show_delete_terminology_term_modal = false;
    MarkTerminologyDocumentDirty(*impl_);
    if (RefreshVisibleTerminologyContextProjection(impl_->app_state))
        impl_->events.Emit(TreeDirtyEvent{});
    SyncTerminologyProblems();
    SetStatus("Deleted term.");
}

void AppRuntime::SelectTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    impl_->selected_terminology_category_ref = category_ref;
}

void AppRuntime::SetTerminologyCategoryFilter(const std::string& category_filter) {
    CopyToBuffer(
        impl_->terminology_category_filter_buf, sizeof(impl_->terminology_category_filter_buf), category_filter);
}

void AppRuntime::BeginAddTerminologyCategory() {
    if (!impl_->app_state.sacm_package.has_value()) {
        SetStatus("Open a terminology package before adding categories.");
        return;
    }
    ClearCategoryEditorBuffers(*impl_);
    impl_->editing_existing_terminology_category = false;
    impl_->show_terminology_category_editor_modal = true;
}

void AppRuntime::BeginEditTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    if (!impl_->app_state.sacm_package.has_value())
        return;
    const sacm::Category* category = core::FindTerminologyCategory(
        impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref, category_ref);
    if (!category) {
        SetStatus("Category not found.");
        return;
    }

    impl_->selected_terminology_category_ref = category_ref;
    CopyCategoryToEditor(*impl_, *category);
    impl_->editing_existing_terminology_category = true;
    impl_->show_terminology_category_editor_modal = true;
}

void AppRuntime::ConfirmTerminologyCategoryEdit() {
    if (!impl_->app_state.sacm_package.has_value())
        return;

    const core::TerminologyCategoryDraft draft = CategoryDraftFromEditor(*impl_);
    std::string error;
    if (impl_->editing_existing_terminology_category) {
        if (!core::UpdateTerminologyCategory(impl_->app_state.sacm_package.value(),
                                             impl_->selected_terminology_package_ref,
                                             impl_->selected_terminology_category_ref,
                                             draft,
                                             error)) {
            SetStatus("Category update failed: " + error);
            return;
        }
        SetStatus("Updated category " + draft.name + ".");
    } else {
        core::TerminologyCategoryCreateResult result = core::CreateTerminologyCategory(
            impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref, draft);
        if (!result.success) {
            SetStatus("Category create failed: " + result.error);
            return;
        }
        impl_->selected_terminology_category_ref = result.category_ref;
        SetStatus("Added category " + draft.name + ".");
    }

    MarkTerminologyDocumentDirty(*impl_);
    impl_->show_terminology_category_editor_modal = false;
}

void AppRuntime::BeginDeleteTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    if (!impl_->app_state.sacm_package.has_value())
        return;
    const sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref);
    if (!terminology_package) {
        SetStatus("Terminology package not found.");
        return;
    }
    const sacm::Category* category = core::FindTerminologyCategory(*terminology_package, category_ref);
    if (!category) {
        SetStatus("Category not found.");
        return;
    }

    impl_->selected_terminology_category_ref = category_ref;
    impl_->pending_delete_terminology_category_term_count =
        core::CountTermsUsingCategory(*terminology_package, category_ref);
    impl_->show_delete_terminology_category_modal = true;
}

void AppRuntime::ConfirmDeleteTerminologyCategory() {
    if (!impl_->app_state.sacm_package.has_value())
        return;

    std::string error;
    if (!core::DeleteTerminologyCategory(impl_->app_state.sacm_package.value(),
                                         impl_->selected_terminology_package_ref,
                                         impl_->selected_terminology_category_ref,
                                         error)) {
        SetStatus("Category delete failed: " + error);
        return;
    }

    impl_->selected_terminology_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(impl_->terminology_category_filter_buf, sizeof(impl_->terminology_category_filter_buf), "");
    impl_->show_delete_terminology_category_modal = false;
    MarkTerminologyDocumentDirty(*impl_);
    SetStatus("Deleted category.");
}

void AppRuntime::SeedRecommendedTerminologyCategories() {
    if (!impl_->app_state.sacm_package.has_value())
        return;

    sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref);
    if (!terminology_package) {
        SetStatus("Terminology package not found.");
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
            impl_->app_state.sacm_package.value(), impl_->selected_terminology_package_ref, draft);
        if (result.success)
            ++added;
    }

    if (added > 0) {
        MarkTerminologyDocumentDirty(*impl_);
        SetStatus("Added recommended terminology categories.");
    } else {
        SetStatus("Recommended terminology categories already exist.");
    }
}

void AppRuntime::RefreshSacmPackageTreeCache() {
    if (!impl_->app_state.current_project.has_value()) {
        impl_->sacm_package_tree_cache.clear();
        return;
    }

    const auto& project = impl_->app_state.current_project.value();
    std::set<std::string> live_paths;
    for (const auto& entry : project.files) {
        if (entry.role != core::ProjectFileRole::SacmArgument)
            continue;
        const std::string relative = entry.relativePath.generic_string();
        live_paths.insert(relative);
        if (impl_->sacm_package_tree_cache.find(relative) != impl_->sacm_package_tree_cache.end())
            continue;
        impl_->sacm_package_tree_cache[relative] = sacm::build_sacm_package_tree(project.rootPath / entry.relativePath);
    }

    for (auto it = impl_->sacm_package_tree_cache.begin(); it != impl_->sacm_package_tree_cache.end();) {
        if (live_paths.count(it->first) == 0) {
            it = impl_->sacm_package_tree_cache.erase(it);
        } else {
            ++it;
        }
    }
}

bool AppRuntime::OpenFirstProjectSacmFile() {
    if (!impl_->app_state.current_project.has_value())
        return false;

    for (const auto& entry : impl_->app_state.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument)
            continue;
        if (entry.state == core::ProjectFileState::Missing)
            continue;
        if (impl_->app_state.open_project_file(entry)) {
            impl_->tree_needs_rebuild = true;
            impl_->pending_focus_root = true;
            impl_->show_gsn_tab = true;
            ui::UiState& ui_state = ui::GetUiState();
            ui_state.center_view = ui::CenterView::GsnCanvas;
            impl_->force_center_tab_selection = true;
            return true;
        }
    }

    SetStatus("Project opened, but no SACM file could be loaded.");
    return false;
}

bool AppRuntime::EnsureReviewItemStorage() {
    if (!impl_->app_state.current_project.has_value()) {
        impl_->review_controller->ClearStorage();
        SyncReviewProblems();
        return false;
    }

    core::AssuranceProject& project = impl_->app_state.current_project.value();
    std::filesystem::path review_path = ReviewItemsPath(project);
    if (review_path.empty()) {
        review_path = project.rootPath / "reviews" / "review-items.af.json";
    }

    std::string error;
    if (impl_->review_controller->ConfigureStorage(review_path, error)) {
        SyncReviewProblems();
        return true;
    }

    SyncReviewProblems();
    SetStatus("Review items could not be loaded: " + error);
    return false;
}

bool AppRuntime::TryOpenProjectManifest(const std::string& selected_path) {
    std::filesystem::path manifest_path(selected_path);
    if (!IsProjectManifestPath(manifest_path)) {
        SetStatus("Please select an af.proj file.");
        return false;
    }
    if (!impl_->app_state.open_project(selected_path)) {
        return false;
    }
    impl_->document_dirty = false;
    impl_->review_controller->ClearDirty();
    impl_->guideline_catalog_load_attempted = false;
    if (impl_->app_state.current_project.has_value()) {
        impl_->proposal_controller->manager.SetProjectRoot(impl_->app_state.current_project->rootPath);
        EnsureReviewItemStorage();
    }
    RefreshSacmPackageTreeCache();
    OpenFirstProjectSacmFile();
    TouchCurrentProjectRecent();
    CopyToBuffer(impl_->project_controller->open_project_path_buf,
                 sizeof(impl_->project_controller->open_project_path_buf),
                 selected_path);
    ImGui::CloseCurrentPopup();
    return true;
}

bool AppRuntime::SaveProject() {
    if (!impl_->app_state.current_project.has_value()) {
        impl_->app_state.status_message = "Create or open a project first.";
        return false;
    }

    if (impl_->review_controller->IsDirty()) {
        core::AssuranceProject& project = impl_->app_state.current_project.value();
        std::string error;
        if (!impl_->review_controller->SaveIfDirty(project, error)) {
            impl_->app_state.status_message = "Review item save failed: " + error;
            return false;
        }
    }

    if (impl_->document_dirty) {
        if (!impl_->app_state.save_project())
            return false;
        impl_->document_dirty = false;
        impl_->app_state.has_unsaved_changes = impl_->review_controller->IsDirty();
        return true;
    }

    if (impl_->app_state.has_unsaved_changes) {
        impl_->app_state.has_unsaved_changes = false;
        impl_->app_state.status_message = "Project saved: " + impl_->app_state.current_project->name;
        return true;
    }

    return impl_->app_state.save_project();
}

} // namespace app
