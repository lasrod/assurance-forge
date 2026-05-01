#include "app/app_runtime.h"
#include "app/app_runtime_state.h"

#include "app/native_file_dialogs.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"

#include "core/project_service.h"
#include "core/reviews/review_item.h"
#include "imgui.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace app {
namespace {

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0) return;
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

}  // namespace

void AppRuntime::BeginCreateProject() {
    std::string selected_path;
    std::string error_message;
    const dialogs::DialogResult result = dialogs::BrowseForProjectParentFolder(
        impl_->project_controller->project_parent_buf, selected_path, error_message);
    if (result == dialogs::DialogResult::Selected) {
        CopyToBuffer(impl_->project_controller->project_parent_buf, sizeof(impl_->project_controller->project_parent_buf), selected_path);
        if (impl_->project_controller->project_name_buf[0] == '\0') {
            CopyToBuffer(impl_->project_controller->project_name_buf, sizeof(impl_->project_controller->project_name_buf), "MySafetyCase");
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
    const dialogs::DialogResult result = dialogs::BrowseForProjectManifest(
        default_path, selected_path, error_message);
    if (result == dialogs::DialogResult::Selected) {
        CopyToBuffer(impl_->project_controller->open_project_path_buf, sizeof(impl_->project_controller->open_project_path_buf), selected_path);
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
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::EvidenceRegister, "evidence-register.af.json");
}

void AppRuntime::BeginCreateProjectJ3377CaeRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->project_controller->BeginProjectFileCreate(ProjectFileCreateKind::J3377CaeRegister, "j3377-cae-register.af.json");
}

void AppRuntime::OpenProjectFile(const core::ProjectFileEntry& entry) {
    if (!impl_->app_state.open_project_file(entry)) return;

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

bool AppRuntime::OpenFirstProjectSacmFile() {
    if (!impl_->app_state.current_project.has_value()) return false;

    for (const auto& entry : impl_->app_state.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument) continue;
        if (entry.state == core::ProjectFileState::Missing) continue;
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
    if (impl_->app_state.current_project.has_value()) {
        impl_->proposal_controller->manager.SetProjectRoot(impl_->app_state.current_project->rootPath);
        EnsureReviewItemStorage();
    }
    OpenFirstProjectSacmFile();
    TouchCurrentProjectRecent();
    CopyToBuffer(impl_->project_controller->open_project_path_buf, sizeof(impl_->project_controller->open_project_path_buf), selected_path);
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
        if (!impl_->app_state.save_project()) return false;
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

}  // namespace app
