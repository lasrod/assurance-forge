#include "app/areas/modal_host.h"

#include "app/mcp_client_config.h"
#include "core/user_settings.h"
#include "app/app_runtime_state.h"
#include "app/project_workflow.h"
#include "app/recent_projects.h"
#include "core/element_factory.h"
#include "core/string_utils.h"
#include "core/terminology_text_utils.h"
#include "hello_imgui/hello_imgui.h"
#include "imgui.h"
#include "ai/secret_store.h"
#include "ui/i18n/localization.h"
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
namespace {

// An `ai::AiConnectionStatus` says what went wrong; the panel only needs to know
// how loudly to say it. Collapsing the error code here keeps `ui` from having to
// know what an `AiErrorCode` is.
ui::panels::AiStatusSeverity ToPanelSeverity(const ai::AiConnectionStatus& status) {
    switch (status.state) {
    case ai::AiTaskState::Running:
        return ui::panels::AiStatusSeverity::Running;
    case ai::AiTaskState::Success:
        return ui::panels::AiStatusSeverity::Success;
    case ai::AiTaskState::Error:
        return ui::panels::AiStatusSeverity::Error;
    case ai::AiTaskState::Idle:
        break;
    }
    // An idle state carrying an error code is still an error: the previous
    // attempt failed and the message on screen is that failure.
    return status.errorCode != ai::AiErrorCode::None ? ui::panels::AiStatusSeverity::Error
                                                     : ui::panels::AiStatusSeverity::Idle;
}

} // namespace

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
    RenderDraftRejectionScopeModal();
    RenderAccessRequestModal();
}

void ModalHost::RenderAccessRequestModal() {
    if (state_.agent_bridge == nullptr)
        return;
    const std::vector<controllers::AccessRequest> requests = state_.agent_bridge->PendingAccessRequests();
    if (requests.empty())
        return;

    // One request at a time, oldest first; answering it reveals the next.
    const controllers::AccessRequest& request = requests.front();
    const std::string project_name =
        state_.app_state.current_project.has_value() ? state_.app_state.current_project->name : std::string();

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("AI client access request") + "###mcp_access_request_modal").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // The label is what the client claims to be -- attribution, not
        // authentication. The token and pipe ACL are the security boundary.
        ImGui::TextUnformatted(
            ui::i18n::trf("\"{0}\" is asking to read the open project and propose draft changes.", request.client_label)
                .c_str());
        ImGui::TextUnformatted(ui::i18n::trf("Project: {0}", project_name).c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted(AF_TR("Nothing is shared until you allow it. Access lasts while this "
                                     "project stays open, and you can withdraw it at any time by "
                                     "disabling MCP in Preferences.")
                                   .c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 160.0f;
        if (ImGui::Button(AF_TR("Allow while open").c_str(), ImVec2(button_width, 0))) {
            state_.agent_bridge->GrantAccess(request.session_id);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Deny").c_str(), ImVec2(button_width, 0))) {
            state_.agent_bridge->DenyAccess(request.session_id);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        // Only when no other popup holds the stack. Two root-level modals
        // both calling OpenPopup every frame replace each other's stack entry
        // and neither ever renders; an access request arrives asynchronously,
        // so unlike the user-triggered modals it must yield and try again
        // once the current popup closes -- the request stays pending.
        ImGui::OpenPopup("###mcp_access_request_modal");
    }
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

    // The panel used to hold `ai::AiProviderSettings*` and an `ai::AiConnectionStatus`,
    // which made `ui` include `ai` -- one of the layer gate's two exceptions.
    // Translating here keeps the AI vocabulary on this side of the boundary.
    //
    // `model_buf` is deliberately NOT reseeded from settings here. This function
    // runs every frame the window is open, and the panel no longer writes each
    // keystroke back into the settings record, so copying settings into the
    // buffer per frame would erase whatever the user was typing before they
    // could finish. The buffer is seeded once at startup and refreshed only
    // where settings change to something the user did not type.
    ui::panels::PreferencesPanelModel model;
    model.aiAvailable = true;
    model.aiEnabled = state_.ai.settings.enabled;
    model.aiProviderName = ai::ToString(state_.ai.settings.provider);
    model.keyStored = state_.ai.key_stored;
    model.secureStoreAvailable = state_.ai.secure_store_available;
    if (!model.secureStoreAvailable) {
        // Two causes, two fixes. Telling a user their platform cannot do this
        // when their build simply has no keyring linked -- or when their keyring
        // is merely not running -- sends them after the wrong problem.
        // Single-line literals so the catalog extractor sees each msgid whole.
        model.secureStoreUnavailableReason =
            ai::SecretStoreBackendName() == "none"
                ? AF_TR("Secure storage is unavailable: this build has no keyring support.")
                : AF_TR("Secure storage is unavailable: no keyring service is running.");
    }
    model.testRunning = test_running;
    model.connectionSeverity = ToPanelSeverity(state_.ai.connection_status);
    model.connectionMessage = state_.ai.connection_status.message;
    model.apiKeyBuffer = state_.ai.api_key_buf;
    model.apiKeyBufferSize = sizeof(state_.ai.api_key_buf);
    model.modelBuffer = state_.ai.model_buf;
    model.modelBufferSize = sizeof(state_.ai.model_buf);
    model.reviewerNameBuffer = state_.reviewer_name_buf;
    model.reviewerNameBufferSize = sizeof(state_.reviewer_name_buf);
    model.theme = ui::GetCurrentAppTheme();
    model.language = ui::i18n::CurrentLanguage();
    model.showDeveloperTools = ui::GetUiState().show_developer_tools;

    model.mcpEnabled = state_.mcp_settings.enabled;
    model.mcpStatus = state_.mcp_status;
    if (state_.app_state.current_project.has_value()) {
        model.mcpClientConfig = app::BuildMcpClientConfig(state_.app_state.current_project->rootPath);
        if (model.mcpClientConfig.empty()) {
            model.mcpConfigUnavailableReason = AF_TR("The MCP server program was not found next to Assurance Forge.");
        }
    } else {
        model.mcpConfigUnavailableReason = AF_TR("Open a project to see its client configuration.");
    }

    ui::panels::PreferencesPanelCallbacks callbacks;
    callbacks.set_ai_enabled = [this](bool enabled) { state_.ai.settings.enabled = enabled; };
    callbacks.save_settings = [this](bool enabled, const char* model_text) {
        state_.ai.settings.enabled = enabled;
        state_.ai.settings.model = model_text != nullptr ? model_text : "";
        if (state_.ai.settings.model.empty())
            state_.ai.settings.model = ai::kDefaultOpenAiModel;
        std::string error;
        if (!state_.ai.service->SaveSettings(state_.ai.settings, error)) {
            state_.ai.connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        CopyToBuffer(state_.ai.model_buf, sizeof(state_.ai.model_buf), state_.ai.settings.model);
        state_.ai.connection_status = ai::SuccessStatus(AF_TR("AI settings saved."));
    };
    callbacks.save_api_key = [this](const char* api_key) {
        if (!api_key || api_key[0] == '\0') {
            state_.ai.connection_status =
                ai::ErrorStatus(ai::AiErrorCode::MissingApiKey, AF_TR("Enter an API key before saving."));
            return;
        }
        ai::SecretStoreResult result = state_.ai.service->SaveApiKey(api_key);
        std::memset(state_.ai.api_key_buf, 0, sizeof(state_.ai.api_key_buf));
        state_.RefreshStoredAiKeyState();
        state_.ai.connection_status = result.success ? ai::SuccessStatus(AF_TR("API key saved securely."))
                                                     : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.remove_api_key = [this]() {
        ai::SecretStoreResult result = state_.ai.service->DeleteApiKey();
        std::memset(state_.ai.api_key_buf, 0, sizeof(state_.ai.api_key_buf));
        state_.RefreshStoredAiKeyState();
        state_.ai.connection_status = result.success ? ai::SuccessStatus(AF_TR("API key removed."))
                                                     : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.test_connection = [this]() {
        if (state_.ai.test_task && state_.ai.test_task->IsRunning())
            return;
        state_.ai.connection_status =
            ai::MakeStatus(ai::AiTaskState::Running, ai::AiErrorCode::None, AF_TR("Testing connection..."));
        state_.ai.settings.model = state_.ai.model_buf;
        if (state_.ai.settings.model.empty()) {
            state_.ai.settings.model = ai::kDefaultOpenAiModel;
            // Testing an empty field substitutes the default, so show the value
            // actually being tested rather than leaving the box blank.
            CopyToBuffer(state_.ai.model_buf, sizeof(state_.ai.model_buf), state_.ai.settings.model);
        }
        std::string error;
        if (!state_.ai.service->SaveSettings(state_.ai.settings, error)) {
            state_.ai.connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        std::shared_ptr<ai::AiService> service = state_.ai.service;
        state_.ai.test_task =
            state_.ai.task_runner.RunConnectionTest([service]() { return service->TestConnection(); });
    };
    callbacks.set_mcp_enabled = [this](bool enabled) {
        core::McpUserSettings settings;
        settings.enabled = enabled;
        std::string error;
        if (!core::SaveMcpUserSettings(state_.ai.settings_store->SettingsPath(), settings, error)) {
            // Surfaced rather than swallowed: otherwise the checkbox flips, fails
            // to save, and reverts next frame with nothing to explain it.
            state_.mcp_status = ui::i18n::trf("Could not save the MCP setting: {0}", error);
            return;
        }
        // Only after the write succeeds: the checkbox must reflect what the MCP
        // server will actually read, not what the user clicked. Closing the
        // master gate also ends every per-session grant -- enforced by the
        // frame loop's sweep in AppRuntime::PollAgentBridge, which watches the
        // flag itself rather than trusting every writer to remember.
        state_.mcp_settings = settings;
        state_.mcp_status.clear();
    };
    callbacks.set_theme = [](ui::AppTheme theme) { ui::ApplyAppTheme(theme); };
    callbacks.set_language = [](ui::i18n::Language language) { ui::i18n::SetLanguage(language); };
    callbacks.set_show_developer_tools = [](bool show_developer_tools) {
        ui::GetUiState().show_developer_tools = show_developer_tools;
    };
    callbacks.save_reviewer_name = [this](const char* reviewer_name) {
        state_.reviewer_name = TrimWhitespace(reviewer_name ? reviewer_name : "");
        CopyToBuffer(state_.reviewer_name_buf, sizeof(state_.reviewer_name_buf), state_.reviewer_name);
        state_.modal_coordinator->show_reviewer_name_prompt = state_.reviewer_name.empty();
        callbacks_.set_status(state_.reviewer_name.empty() ? AF_TR("Reviewer name is required for new reviews.")
                                                           : AF_TR("Reviewer name saved."));
    };

    ui::panels::ShowPreferencesWindow(state_.modal_coordinator->show_preferences_window, model, callbacks);
}

void ModalHost::RenderNotImplementedModal() {
    if (!state_.modal_coordinator->show_not_implemented_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##not_implemented_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // The feature name is stored as the English msgid; translate here so
        // the modal always matches the current UI language.
        ImGui::TextUnformatted(ui::i18n::trf("{0} is not implemented yet.",
                                             AF_TR(state_.modal_coordinator->not_implemented_feature.c_str()))
                                   .c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        float button_width = 100.0f;
        float modal_width = ImGui::GetWindowWidth();
        float center_x = (modal_width - button_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);
        if (ImGui::Button(AF_TR("OK").c_str(), ImVec2(button_width, 0))) {
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
        const std::string mode_label = state_.element_edit_controller->PendingRemoveMode() == core::RemoveMode::NodeOnly
                                           ? AF_TR("this node and its attachments")
                                           : AF_TR("this node and its descendants");
        ImGui::TextUnformatted(ui::i18n::trf("Remove {0}?", mode_label).c_str());
        ImGui::TextUnformatted(ui::i18n::trnf("{0} element will be deleted (highlighted in red).",
                                              "{0} elements will be deleted (highlighted in red).",
                                              n,
                                              n)
                                   .c_str());
        RenderRemovalPreview();
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 110.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button(AF_TR("Remove").c_str(), ImVec2(button_width, 0))) {
            ImGui::CloseCurrentPopup();
            if (state_.app_state.loaded_case.has_value()) {
                state_.element_edit_controller->ConfirmPendingRemoval(state_);
            }
            ui::GetUiState().marked_for_removal.clear();
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.element_edit_controller->ShouldShowRemoveConfirm()) {
        ImGui::OpenPopup("##remove_confirm_modal");
    }
}

// Renders what the SACM library says this removal implies. The count alone
// answers "how many?" but not "which, and what else?" -- and the "what else"
// is exactly the part a user cannot work out from the canvas, because a
// relationship is not a node they selected.
void ModalHost::RenderRemovalPreview() {
    using RemovalEffect = app::controllers::ElementEditController::RemovalEffect;
    const auto& controller = *state_.element_edit_controller;

    if (!controller.PendingRemovePreviewAvailable()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", AF_TR("The SACM library could not preview this removal.").c_str());
        return;
    }

    // `kind` is a SACM class name and `name` is the user's own text; neither is
    // translated. Only the surrounding sentence is.
    const auto render_rows = [](const std::vector<RemovalEffect>& effects) {
        for (const RemovalEffect& effect : effects) {
            const std::string& display = effect.name.empty() ? effect.element_id : effect.name;
            ImGui::Bullet();
            ImGui::TextUnformatted(ui::i18n::trf("{0} ({1})", display, effect.kind).c_str());
        }
    };

    std::vector<RemovalEffect> removed;
    std::vector<RemovalEffect> modified;
    for (const std::vector<RemovalEffect>* bucket :
         {&controller.PendingRemoveTargets(), &controller.PendingRemoveConsequences()}) {
        for (const RemovalEffect& effect : *bucket) {
            (effect.deleted ? removed : modified).push_back(effect);
        }
    }

    if (!removed.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(AF_TR("Will be removed:").c_str());
        render_rows(removed);
    }
    if (!modified.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(AF_TR("Will be modified (references removed):").c_str());
        render_rows(modified);
    }

    const std::vector<std::string>& warnings = controller.PendingRemoveWarnings();
    if (!warnings.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(AF_TR("Reported by the SACM library:").c_str());
        for (const std::string& warning : warnings) {
            ImGui::Bullet();
            ImGui::TextDisabled("%s", warning.c_str());
        }
    }
}

void ModalHost::RenderDeleteReviewItemConfirmModal() {
    if (!state_.review_controller->ShouldShowDeleteConfirm())
        return;

    auto cancel = [&]() { state_.review_controller->CancelDeleteReviewItem(); };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Delete Review Comment") + "###Delete Review Comment").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const core::reviews::ReviewItem& item = state_.review_controller->PendingDeleteReviewItem();
        ImGui::TextWrapped("%s", AF_TR("Delete this review comment?").c_str());
        ImGui::TextWrapped("%s", AF_TR("The attached proposal will also be deleted.").c_str());
        if (item.proposal_id.has_value()) {
            ImGui::TextDisabled("%s", ui::i18n::trf("Proposal: {0}", *item.proposal_id).c_str());
        }
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 130.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button(AF_TR("Delete Both").c_str(), ImVec2(button_width, 0))) {
            core::reviews::ReviewItem pending = state_.review_controller->PendingDeleteReviewItem();
            cancel();
            ImGui::CloseCurrentPopup();
            callbacks_.delete_review_item(pending);
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.review_controller->ShouldShowDeleteConfirm()) {
        ImGui::OpenPopup((AF_TR("Delete Review Comment") + "###Delete Review Comment").c_str());
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
    // The panel owns the shape it draws, so `app` maps onto it here rather than
    // making `ui` include an `app` header.
    std::vector<ui::panels::RecentProjectEntry> recent;
    recent.reserve(state_.project_controller->recent_projects.size());
    for (const app::RecentProjectEntry& entry : state_.project_controller->recent_projects) {
        recent.push_back(ui::panels::RecentProjectEntry{
            entry.name, entry.path, entry.claims, entry.strategies, entry.evidence, entry.undeveloped});
    }

    ui::panels::ShowWelcomeModal(state_.project_controller->show_startup_project_window, recent, callbacks);
}

void ModalHost::RenderCreateProjectModal() {
    if (!state_.project_controller->show_create_project_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Create Empty Assurance Project") + "###Create Empty Assurance Project").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(AF_TR("Project name").c_str());
        ImGui::SetNextItemWidth(420.0f);
        if (ImGui::InputText("##project_name",
                             state_.project_controller->project_name_buf,
                             sizeof(state_.project_controller->project_name_buf))) {
            state_.project_controller->RefreshCreateProjectObstacle();
        }

        ImGui::TextUnformatted(AF_TR("Parent location").c_str());
        ImGui::TextDisabled("%s", state_.project_controller->project_parent_buf);

        // Said while the name is being typed, not after a press that could only
        // fail. An empty name is left unremarked -- the field is visibly empty
        // and nagging about it before anything has been typed helps nobody --
        // but it still holds the button, so Create never does nothing.
        const core::CreateProjectObstacle obstacle = state_.project_controller->create_project_obstacle;
        if (obstacle == core::CreateProjectObstacle::FolderExists) {
            ImGui::TextColored(ui::GetWarningColor(),
                               "%s",
                               ui::i18n::trf("A project named \"{0}\" is already in this folder. Choose another name.",
                                             core::TrimWhitespace(state_.project_controller->project_name_buf))
                                   .c_str());
        } else if (obstacle == core::CreateProjectObstacle::LocationRequired) {
            ImGui::TextColored(ui::GetWarningColor(), "%s", AF_TR("Choose a parent location.").c_str());
        }
        // A create that was attempted and refused anyway: the folder appeared
        // between the check and the press, or the write itself failed.
        if (!state_.project_controller->create_project_error.empty()) {
            ImGui::TextColored(ui::GetErrorColor(), "%s", state_.project_controller->create_project_error.c_str());
        }

        ImGui::Spacing();

        ImGui::BeginDisabled(obstacle != core::CreateProjectObstacle::None);
        if (ImGui::Button(AF_TR("Create").c_str(), ImVec2(110.0f, 0.0f))) {
            if (state_.app_state.create_empty_project(state_.project_controller->project_name_buf,
                                                      state_.project_controller->project_parent_buf)) {
                state_.document_dirty = false;
                state_.review_controller->ClearDirty();
                state_.confidence_controller->ClearDirty();
                state_.register_controller->ClearDirty();
                if (state_.app_state.current_project.has_value()) {
                    state_.proposal_controller->manager.SetProjectRoot(state_.app_state.current_project->rootPath);
                    callbacks_.ensure_project_side_storage();
                }
                callbacks_.open_first_project_sacm_file();
                callbacks_.touch_current_project_recent();
                state_.project_controller->create_project_error.clear();
                state_.project_controller->show_create_project_modal = false;
                ImGui::CloseCurrentPopup();
            } else {
                // Re-asked first (the refresh clears any previous refusal), then
                // the real reason is kept in front of the user instead of going
                // to the status bar, which this very dialog is covering.
                state_.project_controller->RefreshCreateProjectObstacle();
                state_.project_controller->create_project_error = state_.app_state.status_message;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(110.0f, 0.0f))) {
            state_.project_controller->create_project_error.clear();
            state_.project_controller->show_create_project_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.project_controller->show_create_project_modal) {
        // Asked once as the dialog opens, so a name that is already taken is
        // reported before a key is pressed rather than after a dead press.
        state_.project_controller->RefreshCreateProjectObstacle();
        ImGui::OpenPopup((AF_TR("Create Empty Assurance Project") + "###Create Empty Assurance Project").c_str());
    }
}

void ModalHost::RenderProjectFileNameModal() {
    if (!state_.project_controller->show_project_file_name_modal)
        return;

    const char* title_key = ProjectFileCreateTitle(state_.project_controller->pending_project_file_kind);
    const std::string title = AF_TR(title_key) + "###" + title_key;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(AF_TR("File name").c_str());
        ImGui::SetNextItemWidth(420.0f);
        if (ImGui::InputText("##project_file_name",
                             state_.project_controller->project_file_name_buf,
                             sizeof(state_.project_controller->project_file_name_buf))) {
            // The next keystroke is the user answering the refusal.
            state_.project_controller->create_project_file_error.clear();
        }
        if (!state_.project_controller->create_project_file_error.empty()) {
            ImGui::TextColored(ui::GetErrorColor(), "%s", state_.project_controller->create_project_file_error.c_str());
        }
        ImGui::Spacing();

        if (ImGui::Button(AF_TR("Create").c_str(), ImVec2(110.0f, 0.0f))) {
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
                state_.project_controller->create_project_file_error.clear();
                state_.project_controller->show_project_file_name_modal = false;
                ImGui::CloseCurrentPopup();
            } else {
                // The same silence the create-project dialog had: a name that
                // is already taken refused, said so to a status bar this dialog
                // covers, and left the button looking broken.
                state_.project_controller->create_project_file_error = state_.app_state.status_message;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(110.0f, 0.0f))) {
            state_.project_controller->create_project_file_error.clear();
            state_.project_controller->show_project_file_name_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.project_controller->show_project_file_name_modal) {
        ImGui::OpenPopup(title.c_str());
    }
}

void ModalHost::RenderProjectLoadReportModal() {
    auto& report = state_.app_state.last_project_load_report;
    if (!report.showPopup)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Project Loading Status") + "###Project Loading Status").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
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
            ImGui::TextUnformatted(AF_TR("External changes detected").c_str());
            for (const auto& warning : report.warnings) {
                ImGui::BulletText("%s", warning.c_str());
            }
        }
        ImGui::Spacing();
        if (ImGui::Button(AF_TR("OK").c_str(), ImVec2(100.0f, 0.0f))) {
            report.showPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (report.showPopup) {
        ImGui::OpenPopup((AF_TR("Project Loading Status") + "###Project Loading Status").c_str());
    }
}

void ModalHost::RenderSaveBeforeExitModal() {
    if (!state_.modal_coordinator->show_save_before_exit_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            (AF_TR("Unsaved Changes") + "###Unsaved Changes").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", AF_TR("You have unsaved changes. Save before closing?").c_str());
        ImGui::Spacing();

        if (ImGui::Button(AF_TR("Save").c_str(), ImVec2(100.0f, 0.0f))) {
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
        if (ImGui::Button(AF_TR("Don't Save").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.modal_coordinator->show_save_before_exit_modal = false;
            done_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.modal_coordinator->CancelClose();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.modal_coordinator->show_save_before_exit_modal) {
        ImGui::OpenPopup((AF_TR("Unsaved Changes") + "###Unsaved Changes").c_str());
    }
}

void ModalHost::RenderSaveBeforeProjectFileOpenModal() {
    if (!state_.project_controller->show_save_before_project_file_open_modal)
        return;

    const std::string target =
        state_.project_controller->pending_open_project_file_entry.has_value()
            ? state_.project_controller->pending_open_project_file_entry->relativePath.generic_string()
            : AF_TR("the selected project file");

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((AF_TR("Open Project File") + "###Open Project File").c_str(),
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "%s",
            ui::i18n::trf("You have unsaved changes in the current SACM file. Save before opening {0}?", target)
                .c_str());
        ImGui::Spacing();

        if (ImGui::Button(AF_TR("Save").c_str(), ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_pending_project_file_open(true);
            if (!state_.project_controller->show_save_before_project_file_open_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Don't Save").c_str(), ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_pending_project_file_open(false);
            if (!state_.project_controller->show_save_before_project_file_open_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.project_controller->pending_open_project_file_entry.reset();
            state_.project_controller->show_save_before_project_file_open_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.project_controller->show_save_before_project_file_open_modal) {
        ImGui::OpenPopup((AF_TR("Open Project File") + "###Open Project File").c_str());
    }
}

void ModalHost::RenderReviewerNamePromptModal() {
    if (!state_.modal_coordinator->show_reviewer_name_prompt)
        return;
    if (state_.project_controller->show_startup_project_window)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            (AF_TR("Reviewer Name") + "###Reviewer Name").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", AF_TR("Enter the name to use for review comments.").c_str());
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##startup_reviewer_name", state_.reviewer_name_buf, sizeof(state_.reviewer_name_buf));
        ImGui::Spacing();

        const std::string draft = TrimWhitespace(state_.reviewer_name_buf);
        if (draft.empty())
            ImGui::BeginDisabled();
        if (ImGui::Button(AF_TR("Save").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.reviewer_name = draft;
            CopyToBuffer(state_.reviewer_name_buf, sizeof(state_.reviewer_name_buf), state_.reviewer_name);
            state_.modal_coordinator->show_reviewer_name_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        if (draft.empty())
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Later").c_str(), ImVec2(100.0f, 0.0f))) {
            state_.modal_coordinator->show_reviewer_name_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.modal_coordinator->show_reviewer_name_prompt) {
        ImGui::OpenPopup((AF_TR("Reviewer Name") + "###Reviewer Name").c_str());
    }
}

// Rejecting one change can strand others. Which of the two things that means is
// the user's decision, and this is where it is put to them.
//
// The alternative -- cascading silently, which is what this replaces -- discards
// work the user never chose to discard, in a tool where the work is a safety
// argument. Applying only the selection is not an option: the stranded groups
// would fail to materialize and take the whole draft down with them.
void ModalHost::RenderDraftRejectionScopeModal() {
    const AppRuntimeState::PendingDraftRejection& pending = state_.pending_draft_rejection;
    const std::string title = AF_TR("Reject dependent changes?") + "###draft_rejection_scope";
    if (!pending.active) {
        return;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::size_t dependents = pending.dependent_titles.size();
        ImGui::TextWrapped("%s",
                           ui::i18n::trnf("{0} other change is built on the one you are rejecting, so it can no "
                                          "longer be applied on its own.",
                                          "{0} other changes are built on the one you are rejecting, so they can no "
                                          "longer be applied on their own.",
                                          static_cast<int>(dependents),
                                          dependents)
                               .c_str());
        ImGui::Spacing();

        // Both lists are named. A user deciding what to throw away needs to see
        // what is on each side of the decision, not a count.
        ImGui::TextUnformatted(AF_TR("Rejecting:").c_str());
        for (const std::string& title_text : pending.selection_titles) {
            ImGui::Bullet();
            ImGui::TextUnformatted(title_text.c_str());
        }
        ImGui::Spacing();
        ImGui::TextUnformatted(AF_TR("Built on it:").c_str());
        for (const std::string& title_text : pending.dependent_titles) {
            ImGui::Bullet();
            ImGui::TextUnformatted(title_text.c_str());
        }
        ImGui::Spacing();
        ImGui::Spacing();

        // Sized to their labels rather than pinned. These read as sentences, not
        // as "OK"/"Cancel", and at a scaled DPI a fixed width truncated them to
        // "Keep them for rev" and "Reject them to" -- which is the one thing a
        // dialog asking what to destroy must not do.
        if (ImGui::Button(AF_TR("Keep them for review").c_str())) {
            if (callbacks_.resolve_draft_rejection)
                callbacks_.resolve_draft_rejection(false);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s",
                              AF_TR("They stop being applied to the working draft and are marked as needing "
                                    "attention, so their author can retarget them.")
                                  .c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Reject them too").c_str())) {
            if (callbacks_.resolve_draft_rejection)
                callbacks_.resolve_draft_rejection(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(AF_TR("Cancel").c_str())) {
            if (callbacks_.cancel_draft_rejection)
                callbacks_.cancel_draft_rejection();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        ImGui::OpenPopup(title.c_str());
    }
}

void RenderModalHost(AppRuntimeState& state, bool& done, const ModalHostCallbacks& callbacks) {
    ModalHost(state, done, callbacks).Render();
}

} // namespace app::areas
