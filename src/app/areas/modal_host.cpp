#include "app/areas/modal_host.h"
#include "app/app_runtime_state.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "core/element_factory.h"
#include "core/string_utils.h"
#include "core/terminology_text_utils.h"
#include "hello_imgui/hello_imgui.h"
#include "imgui.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/panels/preferences_panel.h"
#include "ui/panels/welcome_modal.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <string>
#include <vector>


#include "app/areas/modal_host_internal.h"

namespace app::areas {

void ModalHost::Render() {
    RenderPreferencesWindow();
    RenderRemoveConfirmModal();
    RenderDeleteReviewItemConfirmModal();
    RenderCreateProjectModal();
    RenderProjectFileNameModal();
    RenderProjectLoadReportModal();
    RenderCreateTerminologyPackageModal();
    RenderDeleteTerminologyPackageModal();
    RenderTerminologyTermEditorModal();
    RenderQuickDefineTermModal();
    RenderDeleteTerminologyTermModal();
    RenderTerminologyCategoryEditorModal();
    RenderDeleteTerminologyCategoryModal();
    RenderSaveBeforeProjectFileOpenModal();
    RenderSaveBeforeExitModal();
    RenderStartupProjectWindow();
    RenderNotImplementedModal();
    RenderReviewerNamePromptModal();
}

void ModalHost::RenderPreferencesWindow() {
    if (!state_.modal_coordinator->show_preferences_window)
        return;

    bool test_running = false;
    if (state_.ai.test_task) {
        ai::AiTaskSnapshot snapshot = state_.ai.test_task->Snapshot();
        test_running = snapshot.state == ai::AiTaskState::Running;
        state_.ai.connection_status = snapshot.status;
        if (!test_running) {
            state_.ai.test_task.reset();
            state_.RefreshStoredAiKeyState();
        }
    }

    ui::panels::PreferencesPanelModel model;
    model.settings = &state_.ai.settings;
    model.keyStored = state_.ai.key_stored;
    model.secureStoreAvailable = state_.ai.secure_store_available;
    model.testRunning = test_running;
    model.connectionStatus = state_.ai.connection_status;
    model.apiKeyBuffer = state_.ai.api_key_buf;
    model.apiKeyBufferSize = sizeof(state_.ai.api_key_buf);
    model.modelBuffer = state_.ai.model_buf;
    model.modelBufferSize = sizeof(state_.ai.model_buf);
    model.reviewerNameBuffer = state_.reviewer_name_buf;
    model.reviewerNameBufferSize = sizeof(state_.reviewer_name_buf);
    model.theme = ui::GetCurrentAppTheme();
    model.language = ui::CurrentLanguage();
    if (HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams()) {
        model.showFps = runner_params->imGuiWindowParams.showStatus_Fps;
    }

    ui::panels::PreferencesPanelCallbacks callbacks;
    callbacks.save_settings = [this](const ai::AiProviderSettings& settings) {
        state_.ai.settings = settings;
        if (state_.ai.settings.model.empty())
            state_.ai.settings.model = ai::kDefaultOpenAiModel;
        std::string error;
        if (!state_.ai.service->SaveSettings(state_.ai.settings, error)) {
            state_.ai.connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        CopyToBuffer(state_.ai.model_buf, sizeof(state_.ai.model_buf), state_.ai.settings.model);
        state_.ai.connection_status = ai::SuccessStatus("AI settings saved.");
    };
    callbacks.save_api_key = [this](const char* api_key) {
        if (!api_key || api_key[0] == '\0') {
            state_.ai.connection_status =
                ai::ErrorStatus(ai::AiErrorCode::MissingApiKey, "Enter an API key before saving.");
            return;
        }
        ai::SecretStoreResult result = state_.ai.service->SaveApiKey(api_key);
        std::memset(state_.ai.api_key_buf, 0, sizeof(state_.ai.api_key_buf));
        state_.RefreshStoredAiKeyState();
        state_.ai.connection_status = result.success ? ai::SuccessStatus("API key saved securely.")
                                                     : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.remove_api_key = [this]() {
        ai::SecretStoreResult result = state_.ai.service->DeleteApiKey();
        std::memset(state_.ai.api_key_buf, 0, sizeof(state_.ai.api_key_buf));
        state_.RefreshStoredAiKeyState();
        state_.ai.connection_status = result.success ? ai::SuccessStatus("API key removed.")
                                                     : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.test_connection = [this]() {
        if (state_.ai.test_task && state_.ai.test_task->IsRunning())
            return;
        state_.ai.connection_status =
            ai::MakeStatus(ai::AiTaskState::Running, ai::AiErrorCode::None, "Testing connection...");
        state_.ai.settings.model = state_.ai.model_buf;
        if (state_.ai.settings.model.empty())
            state_.ai.settings.model = ai::kDefaultOpenAiModel;
        std::string error;
        if (!state_.ai.service->SaveSettings(state_.ai.settings, error)) {
            state_.ai.connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        std::shared_ptr<ai::AiService> service = state_.ai.service;
        state_.ai.test_task =
            state_.ai.task_runner.RunConnectionTest([service]() { return service->TestConnection(); });
    };
    callbacks.set_theme = [](ui::AppTheme theme) { ui::ApplyAppTheme(theme); };
    callbacks.set_language = [](ui::Language language) { ui::SetCurrentLanguage(language); };
    callbacks.set_show_fps = [](bool show_fps) {
        if (HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams()) {
            runner_params->imGuiWindowParams.showStatus_Fps = show_fps;
        }
    };
    callbacks.save_reviewer_name = [this](const char* reviewer_name) {
        state_.reviewer_name = TrimWhitespace(reviewer_name ? reviewer_name : "");
        CopyToBuffer(state_.reviewer_name_buf, sizeof(state_.reviewer_name_buf), state_.reviewer_name);
        state_.modal_coordinator->show_reviewer_name_prompt = state_.reviewer_name.empty();
        callbacks_.set_status(state_.reviewer_name.empty() ? "Reviewer name is required for new reviews."
                                                           : "Reviewer name saved.");
    };

    ui::panels::ShowPreferencesWindow(state_.modal_coordinator->show_preferences_window, model, callbacks);
}

void ModalHost::RenderNotImplementedModal() {
    if (!state_.modal_coordinator->show_not_implemented_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##not_implemented_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s is not implemented yet.", state_.modal_coordinator->not_implemented_feature.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        float button_width = 100.0f;
        float modal_width = ImGui::GetWindowWidth();
        float center_x = (modal_width - button_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);
        if (ImGui::Button("OK", ImVec2(button_width, 0))) {
            state_.modal_coordinator->show_not_implemented_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.modal_coordinator->show_not_implemented_modal) {
        ImGui::OpenPopup("##not_implemented_modal");
    }
}

void ModalHost::RenderRemoveConfirmModal() {
    if (!state_.element_edit_controller->ShouldShowRemoveConfirm())
        return;

    auto cancel = [&]() {
        state_.element_edit_controller->CancelPendingRemoval();
        ui::GetUiState().marked_for_removal.clear();
    };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##remove_confirm_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int n = static_cast<int>(state_.element_edit_controller->PendingRemoveIds().size());
        const char* mode_label = state_.element_edit_controller->PendingRemoveMode() == core::RemoveMode::NodeOnly
                                     ? "this node and its attachments"
                                     : "this node and its descendants";
        ImGui::Text("Remove %s?", mode_label);
        ImGui::Text("%d element%s will be deleted (highlighted in red).", n, n == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 110.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button("Remove", ImVec2(button_width, 0))) {
            ImGui::CloseCurrentPopup();
            if (state_.app_state.loaded_case.has_value()) {
                parser::AssuranceCase& ac = state_.app_state.loaded_case.value();
                sacm::AssuranceCasePackage* pkg =
                    state_.app_state.sacm_package.has_value() ? &state_.app_state.sacm_package.value() : nullptr;
                state_.element_edit_controller->ConfirmPendingRemoval(ac, pkg);
            }
            ui::GetUiState().marked_for_removal.clear();
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.element_edit_controller->ShouldShowRemoveConfirm()) {
        ImGui::OpenPopup("##remove_confirm_modal");
    }
}

void ModalHost::RenderDeleteReviewItemConfirmModal() {
    if (!state_.review_controller->ShouldShowDeleteConfirm())
        return;

    auto cancel = [&]() { state_.review_controller->CancelDeleteReviewItem(); };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Review Comment", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const core::reviews::ReviewItem& item = state_.review_controller->PendingDeleteReviewItem();
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
            core::reviews::ReviewItem pending = state_.review_controller->PendingDeleteReviewItem();
            cancel();
            ImGui::CloseCurrentPopup();
            callbacks_.delete_review_item(pending);
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.review_controller->ShouldShowDeleteConfirm()) {
        ImGui::OpenPopup("Delete Review Comment");
    }
}

void ModalHost::RenderStartupProjectWindow() {
    ui::panels::WelcomeModalCallbacks callbacks{
        [this]() {
            callbacks_.begin_create_project();
            if (state_.project_controller->show_create_project_modal) {
                state_.project_controller->show_startup_project_window = false;
            }
        },
        []() {},
        [this]() { callbacks_.begin_open_project(); },
        []() {},
        []() {},
        []() {},
        []() {},
        [this](const ui::panels::RecentProjectEntry& entry) {
            if (!callbacks_.try_open_project_manifest(entry.path)) {
                state_.project_controller->RemoveRecentProjectByPath(entry.path);
            }
        },
    };
    ui::panels::ShowWelcomeModal(
        state_.project_controller->show_startup_project_window, state_.project_controller->recent_projects, callbacks);
}

void ModalHost::RenderCreateProjectModal() {
    if (!state_.project_controller->show_create_project_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create Empty Assurance Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Project name");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##project_name",
                         state_.project_controller->project_name_buf,
                         sizeof(state_.project_controller->project_name_buf));

        ImGui::TextUnformatted("Parent location");
        ImGui::TextDisabled("%s", state_.project_controller->project_parent_buf);

        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
            if (state_.app_state.create_empty_project(state_.project_controller->project_name_buf,
                                                      state_.project_controller->project_parent_buf)) {
                state_.document_dirty = false;
                state_.review_controller->ClearDirty();
                if (state_.app_state.current_project.has_value()) {
                    state_.proposal_controller->manager.SetProjectRoot(state_.app_state.current_project->rootPath);
                    callbacks_.ensure_review_item_storage();
                }
                callbacks_.open_first_project_sacm_file();
                callbacks_.touch_current_project_recent();
                state_.project_controller->show_create_project_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            state_.project_controller->show_create_project_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.project_controller->show_create_project_modal) {
        ImGui::OpenPopup("Create Empty Assurance Project");
    }
}

void ModalHost::RenderProjectFileNameModal() {
    if (!state_.project_controller->show_project_file_name_modal)
        return;

    const char* title = ProjectFileCreateTitle(state_.project_controller->pending_project_file_kind);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("File name");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##project_file_name",
                         state_.project_controller->project_file_name_buf,
                         sizeof(state_.project_controller->project_file_name_buf));
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
            bool created = false;
            if (state_.project_controller->pending_project_file_kind == ProjectFileCreateKind::Sacm) {
                created = state_.app_state.create_project_sacm_file(state_.project_controller->project_file_name_buf);
            } else if (state_.project_controller->pending_project_file_kind ==
                       ProjectFileCreateKind::EvidenceRegister) {
                created =
                    state_.app_state.create_project_evidence_register(state_.project_controller->project_file_name_buf);
            } else {
                created = state_.app_state.create_project_j3377_cae_register(
                    state_.project_controller->project_file_name_buf);
            }
            if (created) {
                // Newly created SACM files are opened immediately so the
                // audit command bus gets installed for the rest of the
                // session; otherwise subsequent edits fall through the
                // legacy direct-mutation path and the history timeline
                // stays empty.
                if (state_.project_controller->pending_project_file_kind == ProjectFileCreateKind::Sacm &&
                    callbacks_.open_first_project_sacm_file) {
                    callbacks_.open_first_project_sacm_file();
                }
                state_.project_controller->show_project_file_name_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            state_.project_controller->show_project_file_name_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.project_controller->show_project_file_name_modal) {
        ImGui::OpenPopup(title);
    }
}

void ModalHost::RenderProjectLoadReportModal() {
    auto& report = state_.app_state.last_project_load_report;
    if (!report.showPopup)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Project Loading Status", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        for (const auto& step : report.steps) {
            const char* mark = "[OK]";
            if (step.status == core::ProjectLoadStepStatus::Failed)
                mark = "[FAIL]";
            if (step.status == core::ProjectLoadStepStatus::Warning)
                mark = "[WARN]";
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

void ModalHost::RenderSaveBeforeExitModal() {
    if (!state_.modal_coordinator->show_save_before_exit_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("You have unsaved changes. Save before closing?");
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            bool saved = false;
            if (state_.app_state.current_project.has_value()) {
                saved = callbacks_.save_project();
            } else {
                saved = state_.app_state.save_current_document();
            }

            if (saved) {
                state_.modal_coordinator->show_save_before_exit_modal = false;
                done_ = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(100.0f, 0.0f))) {
            state_.modal_coordinator->show_save_before_exit_modal = false;
            done_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.modal_coordinator->CancelClose();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.modal_coordinator->show_save_before_exit_modal) {
        ImGui::OpenPopup("Unsaved Changes");
    }
}

void ModalHost::RenderSaveBeforeProjectFileOpenModal() {
    if (!state_.project_controller->show_save_before_project_file_open_modal)
        return;

    const std::string target =
        state_.project_controller->pending_open_project_file_entry.has_value()
            ? state_.project_controller->pending_open_project_file_entry->relativePath.generic_string()
            : std::string{"the selected project file"};

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Open Project File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("You have unsaved changes in the current SACM file. Save before opening %s?",
                           target.c_str());
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_pending_project_file_open(true);
            if (!state_.project_controller->show_save_before_project_file_open_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_pending_project_file_open(false);
            if (!state_.project_controller->show_save_before_project_file_open_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.project_controller->pending_open_project_file_entry.reset();
            state_.project_controller->show_save_before_project_file_open_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.project_controller->show_save_before_project_file_open_modal) {
        ImGui::OpenPopup("Open Project File");
    }
}

void ModalHost::RenderReviewerNamePromptModal() {
    if (!state_.modal_coordinator->show_reviewer_name_prompt)
        return;
    if (state_.project_controller->show_startup_project_window)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Reviewer Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Enter the name to use for review comments.");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##startup_reviewer_name", state_.reviewer_name_buf, sizeof(state_.reviewer_name_buf));
        ImGui::Spacing();

        const std::string draft = TrimWhitespace(state_.reviewer_name_buf);
        if (draft.empty())
            ImGui::BeginDisabled();
        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            state_.reviewer_name = draft;
            CopyToBuffer(state_.reviewer_name_buf, sizeof(state_.reviewer_name_buf), state_.reviewer_name);
            state_.modal_coordinator->show_reviewer_name_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        if (draft.empty())
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(100.0f, 0.0f))) {
            state_.modal_coordinator->show_reviewer_name_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.modal_coordinator->show_reviewer_name_prompt) {
        ImGui::OpenPopup("Reviewer Name");
    }
}

void RenderModalHost(AppRuntimeState& state, bool& done, const ModalHostCallbacks& callbacks) {
    ModalHost(state, done, callbacks).Render();
}

} // namespace app::areas
