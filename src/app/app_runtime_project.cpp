#include "app/app_runtime.h"
#include "app/actions/terminology_actions.h"
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
#include <cstring>
#include <filesystem>
#include <set>
#include <string>

namespace app {
namespace {

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0)
        return;
    const size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
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

void CopyTerminologyPackageToEditor(AppRuntimeState& state, const sacm::TerminologyPackage& package) {
    CopyToBuffer(state.terminology.package_name_buf, sizeof(state.terminology.package_name_buf), package.name);
    CopyToBuffer(state.terminology.package_description_buf,
                 sizeof(state.terminology.package_description_buf),
                 package.description);
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
        impl_->workbench.pending_focus_root = true;
        impl_->workbench.show_gsn_tab = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->workbench.force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::EvidenceRegister) {
        impl_->workbench.show_evidence_tab = true;
        ui_state.center_view = ui::CenterView::EvidenceRegister;
        impl_->workbench.force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::J3377CaeRegister) {
        impl_->workbench.show_cse_tab = true;
        ui_state.center_view = ui::CenterView::CseRegister;
        impl_->workbench.force_center_tab_selection = true;
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
        impl_->workbench.pending_focus_root = false;
        impl_->workbench.show_gsn_tab = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->workbench.force_center_tab_selection = true;

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

        impl_->terminology.selected_package_ref = package_ref;
        impl_->terminology.selected_package_file_path = impl_->selected_package_file_path;
        impl_->terminology.selected_term_ref = core::TerminologyTermRef{};
        impl_->terminology.selected_category_ref = core::TerminologyCategoryRef{};
        CopyToBuffer(impl_->terminology.category_filter_buf, sizeof(impl_->terminology.category_filter_buf), "");
        CopyTerminologyPackageToEditor(*impl_, *terminology_package);
        impl_->workbench.show_terminology_package_tab = true;
        ui_state.center_view = ui::CenterView::TerminologyPackage;
        impl_->workbench.force_center_tab_selection = true;
        return;
    }

    impl_->workbench.show_package_details_tab = true;
    ui_state.center_view = ui::CenterView::PackageDetails;
    impl_->workbench.force_center_tab_selection = true;
}

void AppRuntime::BeginAddTerminologyPackage(const core::ProjectFileEntry& entry,
                                            const sacm::SacmPackageTreeNode& parent_node) {
    TerminologyActions(*impl_).BeginAddPackage(entry, parent_node);
}

void AppRuntime::ConfirmAddTerminologyPackage() {
    if (TerminologyActions(*impl_).ConfirmAddPackage())
        SyncTerminologyProblems();
}

void AppRuntime::ApplyTerminologyPackageEdits() {
    if (TerminologyActions(*impl_).ApplyPackageEdits())
        SyncTerminologyProblems();
}

void AppRuntime::BeginDeleteTerminologyPackage() {
    TerminologyActions(*impl_).BeginDeletePackage();
}

void AppRuntime::ConfirmDeleteTerminologyPackage() {
    if (TerminologyActions(*impl_).ConfirmDeletePackage())
        SyncTerminologyProblems();
}

void AppRuntime::SelectTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    TerminologyActions(*impl_).SelectTerm(term_ref);
}

void AppRuntime::BeginAddTerminologyTerm() {
    TerminologyActions(*impl_).BeginAddTerm();
}

void AppRuntime::BeginEditTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    TerminologyActions(*impl_).BeginEditTerm(term_ref);
}

void AppRuntime::ConfirmTerminologyTermEdit() {
    if (TerminologyActions(*impl_).ConfirmTermEdit())
        SyncTerminologyProblems();
}

void AppRuntime::OpenTerminologyTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                               const core::TerminologyTermRef& term_ref) {
    TerminologyActions(*impl_).OpenTermFromCanvas(package_ref, term_ref);
}

void AppRuntime::EditTerminologyTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                               const core::TerminologyTermRef& term_ref) {
    TerminologyActions(*impl_).EditTermFromCanvas(package_ref, term_ref);
}

void AppRuntime::AddTerminologyTermAsContextFromCanvas(const std::string& element_id,
                                                       const core::TerminologyPackageRef& package_ref,
                                                       const core::TerminologyTermRef& term_ref) {
    if (TerminologyActions(*impl_).AddTermAsContextFromCanvas(element_id, package_ref, term_ref))
        SyncTerminologyProblems();
}

void AppRuntime::AddVisibleTerminologyTermContextFromCanvas(const std::string& element_id,
                                                            const core::TerminologyPackageRef& package_ref,
                                                            const core::TerminologyTermRef& term_ref) {
    if (TerminologyActions(*impl_).AddVisibleTermContextFromCanvas(element_id, package_ref, term_ref))
        SyncTerminologyProblems();
}

void AppRuntime::FindTerminologyUsagesFromCanvas(const core::TerminologyPackageRef& package_ref,
                                                 const core::TerminologyTermRef& term_ref) {
    TerminologyActions(*impl_).BeginFindUsages(package_ref, term_ref);
}

void AppRuntime::BeginFindTerminologyUsages(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    TerminologyActions(*impl_).BeginFindUsages(package_ref, term_ref);
}

void AppRuntime::NavigateToTerminologyUsage(std::size_t usage_index) {
    TerminologyActions(*impl_).NavigateToUsage(usage_index);
}

void AppRuntime::ChangeTerminologyMeaningFromCanvas(const std::string& element_id, const std::string& term_value) {
    TerminologyActions(*impl_).ChangeMeaningFromCanvas(element_id, term_value);
}

void AppRuntime::BeginQuickDefineTerminologyTerm(const std::string& element_id, const std::string& term_value) {
    TerminologyActions(*impl_).BeginQuickDefineTerm(element_id, term_value);
}

void AppRuntime::BeginLinkExistingTerminologyTerm(const std::string& element_id, const std::string& term_value) {
    TerminologyActions(*impl_).BeginLinkExistingTerm(element_id, term_value);
}

void AppRuntime::IgnoreTerminologySuggestion(const std::string& element_id, const std::string& term_value) {
    TerminologyActions(*impl_).IgnoreSuggestion(element_id, term_value);
}

bool AppRuntime::IsTerminologySuggestionIgnored(const std::string& element_id, const std::string& term_value) const {
    return TerminologyActions(*impl_).IsSuggestionIgnored(element_id, term_value);
}

void AppRuntime::HandleProblemQuickFix(const core::ProblemItem& problem) {
    TerminologyActions(*impl_).HandleProblemQuickFix(problem);
}

void AppRuntime::ConfirmQuickDefineTerminologyTerm(bool add_as_context) {
    if (TerminologyActions(*impl_).ConfirmQuickDefineTerm(add_as_context))
        SyncTerminologyProblems();
}

void AppRuntime::BeginDeleteTerminologyTerm(const core::TerminologyTermRef& term_ref) {
    TerminologyActions(*impl_).BeginDeleteTerm(term_ref);
}

void AppRuntime::ConfirmDeleteTerminologyTerm() {
    if (TerminologyActions(*impl_).ConfirmDeleteTerm())
        SyncTerminologyProblems();
}

void AppRuntime::SelectTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    TerminologyActions(*impl_).SelectCategory(category_ref);
}

void AppRuntime::SetTerminologyCategoryFilter(const std::string& category_filter) {
    TerminologyActions(*impl_).SetCategoryFilter(category_filter);
}

void AppRuntime::BeginAddTerminologyCategory() {
    TerminologyActions(*impl_).BeginAddCategory();
}

void AppRuntime::BeginEditTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    TerminologyActions(*impl_).BeginEditCategory(category_ref);
}

void AppRuntime::ConfirmTerminologyCategoryEdit() {
    TerminologyActions(*impl_).ConfirmCategoryEdit();
}

void AppRuntime::BeginDeleteTerminologyCategory(const core::TerminologyCategoryRef& category_ref) {
    TerminologyActions(*impl_).BeginDeleteCategory(category_ref);
}

void AppRuntime::ConfirmDeleteTerminologyCategory() {
    TerminologyActions(*impl_).ConfirmDeleteCategory();
}

void AppRuntime::SeedRecommendedTerminologyCategories() {
    TerminologyActions(*impl_).SeedRecommendedCategories();
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
            impl_->workbench.pending_focus_root = true;
            impl_->workbench.show_gsn_tab = true;
            ui::UiState& ui_state = ui::GetUiState();
            ui_state.center_view = ui::CenterView::GsnCanvas;
            impl_->workbench.force_center_tab_selection = true;
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
