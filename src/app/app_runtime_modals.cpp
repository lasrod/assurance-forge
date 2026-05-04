#include "app/app_runtime.h"
#include "app/app_runtime_state.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"

#include "core/element_factory.h"
#include "imgui.h"
#include "ui/panels/welcome_modal.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace app {
namespace {

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0) return;
    const size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    return std::string(begin, end);
}

}  // namespace

void AppRuntime::RenderNotImplementedModal() {
    if (!impl_->modal_coordinator->show_not_implemented_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##not_implemented_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s is not implemented yet.", impl_->modal_coordinator->not_implemented_feature.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        float button_width = 100.0f;
        float modal_width = ImGui::GetWindowWidth();
        float center_x = (modal_width - button_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);
        if (ImGui::Button("OK", ImVec2(button_width, 0))) {
            impl_->modal_coordinator->show_not_implemented_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->modal_coordinator->show_not_implemented_modal) {
        ImGui::OpenPopup("##not_implemented_modal");
    }
}

void AppRuntime::RenderRemoveConfirmModal() {
    if (!impl_->element_edit_controller->ShouldShowRemoveConfirm()) return;

    auto cancel = [&]() {
        impl_->element_edit_controller->CancelPendingRemoval();
        ui::GetUiState().marked_for_removal.clear();
    };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##remove_confirm_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int n = static_cast<int>(impl_->element_edit_controller->PendingRemoveIds().size());
        const char* mode_label =
            impl_->element_edit_controller->PendingRemoveMode() == core::RemoveMode::NodeOnly
                ? "this node and its attachments"
                : "this node and its descendants";
        ImGui::Text("Remove %s?", mode_label);
        ImGui::Text("%d element%s will be deleted (highlighted in red).",
                    n, n == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 110.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button("Remove", ImVec2(button_width, 0))) {
            ImGui::CloseCurrentPopup();
            if (impl_->app_state.loaded_case.has_value()) {
                parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
                sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                                      ? &impl_->app_state.sacm_package.value()
                                                      : nullptr;
                impl_->element_edit_controller->ConfirmPendingRemoval(ac, pkg);
            }
            ui::GetUiState().marked_for_removal.clear();
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->element_edit_controller->ShouldShowRemoveConfirm()) {
        ImGui::OpenPopup("##remove_confirm_modal");
    }
}

void AppRuntime::RenderDeleteReviewItemConfirmModal() {
    if (!impl_->review_controller->ShouldShowDeleteConfirm()) return;

    auto cancel = [&]() {
        impl_->review_controller->CancelDeleteReviewItem();
    };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Review Comment", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const core::reviews::ReviewItem& item = impl_->review_controller->PendingDeleteReviewItem();
        ImGui::TextWrapped("Delete this review comment?");
        ImGui::TextWrapped("The attached proposal will also be deleted.");
        if (item.proposal_id.has_value()) {
            ImGui::TextDisabled("Proposal: %s", item.proposal_id->c_str());
        }
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 130.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button("Delete Both", ImVec2(button_width, 0))) {
            core::reviews::ReviewItem pending = impl_->review_controller->PendingDeleteReviewItem();
            cancel();
            ImGui::CloseCurrentPopup();
            DeleteReviewItem(pending);
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->review_controller->ShouldShowDeleteConfirm()) {
        ImGui::OpenPopup("Delete Review Comment");
    }
}

void AppRuntime::RenderStartupProjectWindow() {
    ui::panels::WelcomeModalCallbacks callbacks{
        [this]() { BeginCreateProject(); },
        []() {},
        [this]() { BeginOpenProject(); },
        []() {},
        []() {},
        []() {},
        []() {},
        [this](const ui::panels::RecentProjectEntry& entry) {
            if (!TryOpenProjectManifest(entry.path)) {
                impl_->project_controller->RemoveRecentProjectByPath(entry.path);
            }
        },
    };
    ui::panels::ShowWelcomeModal(impl_->project_controller->show_startup_project_window,
                                 impl_->project_controller->recent_projects,
                                 callbacks);
}

void AppRuntime::RenderCreateProjectModal() {
    if (!impl_->project_controller->show_create_project_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create Empty Assurance Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Project name");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##project_name", impl_->project_controller->project_name_buf, sizeof(impl_->project_controller->project_name_buf));

        ImGui::TextUnformatted("Parent location");
        ImGui::TextDisabled("%s", impl_->project_controller->project_parent_buf);

        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
            if (impl_->app_state.create_empty_project(impl_->project_controller->project_name_buf,
                                                      impl_->project_controller->project_parent_buf)) {
                impl_->document_dirty = false;
                impl_->review_controller->ClearDirty();
                if (impl_->app_state.current_project.has_value()) {
                    impl_->proposal_controller->manager.SetProjectRoot(impl_->app_state.current_project->rootPath);
                    EnsureReviewItemStorage();
                }
                OpenFirstProjectSacmFile();
                TouchCurrentProjectRecent();
                impl_->project_controller->show_create_project_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            impl_->project_controller->show_create_project_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->project_controller->show_create_project_modal) {
        ImGui::OpenPopup("Create Empty Assurance Project");
    }
}

void AppRuntime::RenderProjectFileNameModal() {
    if (!impl_->project_controller->show_project_file_name_modal) return;

    const char* title = ProjectFileCreateTitle(impl_->project_controller->pending_project_file_kind);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("File name");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##project_file_name", impl_->project_controller->project_file_name_buf, sizeof(impl_->project_controller->project_file_name_buf));
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
            bool created = false;
            if (impl_->project_controller->pending_project_file_kind == ProjectFileCreateKind::Sacm) {
                created = impl_->app_state.create_project_sacm_file(impl_->project_controller->project_file_name_buf);
            } else if (impl_->project_controller->pending_project_file_kind == ProjectFileCreateKind::EvidenceRegister) {
                created = impl_->app_state.create_project_evidence_register(impl_->project_controller->project_file_name_buf);
            } else {
                created = impl_->app_state.create_project_j3377_cae_register(impl_->project_controller->project_file_name_buf);
            }
            if (created) {
                impl_->project_controller->show_project_file_name_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            impl_->project_controller->show_project_file_name_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->project_controller->show_project_file_name_modal) {
        ImGui::OpenPopup(title);
    }
}

void AppRuntime::RenderProjectLoadReportModal() {
    auto& report = impl_->app_state.last_project_load_report;
    if (!report.showPopup) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Project Loading Status", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        for (const auto& step : report.steps) {
            const char* mark = "[OK]";
            if (step.status == core::ProjectLoadStepStatus::Failed) mark = "[FAIL]";
            if (step.status == core::ProjectLoadStepStatus::Warning) mark = "[WARN]";
            ImGui::Text("%s %s", mark, step.label.c_str());
            if (!step.message.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", step.message.c_str());
            }
        }
        if (!report.warnings.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("External changes detected");
            for (const auto& warning : report.warnings) {
                ImGui::BulletText("%s", warning.c_str());
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) {
            report.showPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (report.showPopup) {
        ImGui::OpenPopup("Project Loading Status");
    }
}

void AppRuntime::RenderSaveBeforeExitModal(bool& done) {
    if (!impl_->modal_coordinator->show_save_before_exit_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("You have unsaved changes. Save before closing?");
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            bool saved = false;
            if (impl_->app_state.current_project.has_value()) {
                saved = SaveProject();
            } else {
                saved = impl_->app_state.save_current_document();
            }

            if (saved) {
                impl_->modal_coordinator->show_save_before_exit_modal = false;
                done = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(100.0f, 0.0f))) {
            impl_->modal_coordinator->show_save_before_exit_modal = false;
            done = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->modal_coordinator->show_save_before_exit_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->modal_coordinator->show_save_before_exit_modal) {
        ImGui::OpenPopup("Unsaved Changes");
    }
}

void AppRuntime::RenderReviewerNamePromptModal() {
    if (!impl_->modal_coordinator->show_reviewer_name_prompt) return;
    if (impl_->project_controller->show_startup_project_window) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Reviewer Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Enter the name to use for review comments.");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##startup_reviewer_name", impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf));
        ImGui::Spacing();

        const std::string draft = TrimWhitespace(impl_->reviewer_name_buf);
        if (draft.empty()) ImGui::BeginDisabled();
        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            impl_->reviewer_name = draft;
            CopyToBuffer(impl_->reviewer_name_buf, sizeof(impl_->reviewer_name_buf), impl_->reviewer_name);
            impl_->modal_coordinator->show_reviewer_name_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        if (draft.empty()) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(100.0f, 0.0f))) {
            impl_->modal_coordinator->show_reviewer_name_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->modal_coordinator->show_reviewer_name_prompt) {
        ImGui::OpenPopup("Reviewer Name");
    }
}

}  // namespace app
