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
#include <sstream>
#include <set>
#include <string>
#include <system_error>
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

void MarkTerminologyDocumentDirty(AppRuntimeState& state) {
    state.app_state.mark_dirty();
    state.document_dirty = true;
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
    impl_->selected_terminology_package_file_path = impl_->app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package =
            core::FindTerminologyPackage(impl_->app_state.sacm_package.value(), result.package_ref)) {
        CopyTerminologyPackageToEditor(*impl_, *package);
    }
    MarkTerminologyDocumentDirty(*impl_);
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
    impl_->show_terminology_term_editor_modal = false;
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
    SetStatus("Deleted term.");
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
