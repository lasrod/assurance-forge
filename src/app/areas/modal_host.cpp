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
#include "ui/ui_state.h"

#include <algorithm>
#include <string>
#include <vector>

namespace app::areas {
namespace {

using core::TrimWhitespace;
using ui::CopyToBuffer;

bool SameTermRef(const sacm::Term& term, const core::TerminologyTermRef& term_ref) {
    if (!term_ref.id.empty() && term.id == term_ref.id)
        return true;
    if (!term_ref.gid.empty() && term.gid == term_ref.gid)
        return true;
    return false;
}

bool HasTerminologyPackageRef(const core::TerminologyPackageRef& package_ref) {
    return !package_ref.id.empty() || !package_ref.gid.empty();
}

bool SameTerminologyPackageRef(const core::TerminologyPackageRef& left, const core::TerminologyPackageRef& right) {
    if (!left.id.empty() && !right.id.empty() && left.id == right.id)
        return true;
    if (!left.gid.empty() && !right.gid.empty() && left.gid == right.gid)
        return true;
    return false;
}

bool TermDefinitionHasDuplicate(const AppRuntimeState& state,
                                const core::TerminologyPackageRef& package_ref,
                                const std::string& value,
                                const std::string& description,
                                bool editing_existing_term,
                                const core::TerminologyTermRef& selected_term_ref) {
    if (value.empty() || description.empty() || !state.app_state.sacm_package.has_value())
        return false;
    const sacm::TerminologyPackage* package =
        core::FindTerminologyPackage(state.app_state.sacm_package.value(), package_ref);
    if (!package)
        return false;
    for (const auto& term : package->terms) {
        if (editing_existing_term && SameTermRef(term, selected_term_ref))
            continue;
        if (TrimWhitespace(term.value) == value && TrimWhitespace(term.description) == description)
            return true;
    }
    return false;
}

bool CurrentTermDefinitionHasDuplicate(const AppRuntimeState& state,
                                       const std::string& value,
                                       const std::string& description) {
    return TermDefinitionHasDuplicate(state,
                                      state.terminology.selected_package_ref,
                                      value,
                                      description,
                                      state.terminology.editing_existing_term,
                                      state.terminology.selected_term_ref);
}

bool ContainsCategoryRef(const std::vector<std::string>& refs, const std::string& ref) {
    return std::find(refs.begin(), refs.end(), ref) != refs.end();
}

void SetCategoryChecked(AppRuntimeState& state, const sacm::Category& category, bool checked) {
    std::vector<std::string> refs = core::SplitNormalizedCategoryRefs(state.terminology.term_categories_buf);
    const std::string ref = !category.id.empty() ? category.id : category.gid;
    if (checked) {
        if (!ContainsCategoryRef(refs, ref))
            refs.push_back(ref);
    } else {
        refs.erase(std::remove(refs.begin(), refs.end(), ref), refs.end());
        if (!category.id.empty())
            refs.erase(std::remove(refs.begin(), refs.end(), category.id), refs.end());
        if (!category.gid.empty())
            refs.erase(std::remove(refs.begin(), refs.end(), category.gid), refs.end());
    }
    CopyToBuffer(
        state.terminology.term_categories_buf, sizeof(state.terminology.term_categories_buf), core::JoinCategoryRefs(refs));
}

void RenderTermCategoryPickerForPackage(AppRuntimeState& state, const core::TerminologyPackageRef& package_ref) {
    const sacm::TerminologyPackage* package = nullptr;
    if (state.app_state.sacm_package.has_value()) {
        package = core::FindTerminologyPackage(state.app_state.sacm_package.value(), package_ref);
    }

    ImGui::TextUnformatted("Categories");
    if (!package || package->categories.empty()) {
        ImGui::TextDisabled("No categories are available in this terminology package.");
        return;
    }

    std::vector<std::string> refs = core::SplitNormalizedCategoryRefs(state.terminology.term_categories_buf);
    const float list_height = ImGui::GetTextLineHeightWithSpacing() * 5.0f;
    if (ImGui::BeginChild("##term_category_picker", ImVec2(460.0f, list_height), true)) {
        for (const auto& category : package->categories) {
            const std::string ref = !category.id.empty() ? category.id : category.gid;
            bool selected = ContainsCategoryRef(refs, ref) || ContainsCategoryRef(refs, category.gid);
            const std::string label = (category.name.empty() ? ref : category.name) + "##" + ref;
            if (ImGui::Checkbox(label.c_str(), &selected))
                SetCategoryChecked(state, category, selected);
        }
    }
    ImGui::EndChild();
}

void RenderTermCategoryPicker(AppRuntimeState& state) {
    RenderTermCategoryPickerForPackage(state, state.terminology.selected_package_ref);
}

void RenderTermTextFields(AppRuntimeState& state) {
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputText("Term", state.terminology.term_value_buf, sizeof(state.terminology.term_value_buf));
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputText(
        "Full Name / Display Name", state.terminology.term_name_buf, sizeof(state.terminology.term_name_buf));
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputTextMultiline("Definition",
                              state.terminology.term_definition_buf,
                              sizeof(state.terminology.term_definition_buf),
                              ImVec2(460.0f, 110.0f));
}

void RenderTermExternalReferenceField(AppRuntimeState& state) {
    ImGui::SetNextItemWidth(460.0f);
    ImGui::InputText("External Reference",
                     state.terminology.term_external_reference_buf,
                     sizeof(state.terminology.term_external_reference_buf));
}

void RenderTerminologyTermValidationMessages(bool missing_value,
                                             bool missing_target_package,
                                             bool duplicate_definition,
                                             bool missing_definition,
                                             bool missing_category) {
    if (missing_value)
        ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Term value is required.");
    if (missing_target_package)
        ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Choose a target TerminologyPackage.");
    if (duplicate_definition)
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.15f, 1.0f),
                           "Duplicate term value and definition exist in this package.");
    if (missing_definition)
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.15f, 1.0f), "Concrete term has no description.");
    if (missing_category)
        ImGui::TextDisabled("Term has no category.");
}

struct TerminologyPackageChoice {
    core::TerminologyPackageRef ref;
    std::string label;
};

core::TerminologyPackageRef TerminologyPackageRefFor(const sacm::TerminologyPackage& package) {
    return core::TerminologyPackageRef{package.id, package.gid};
}

std::string PackageDisplayLabel(const sacm::TerminologyPackage& package, const std::string& scope_label) {
    const std::string fallback = !package.id.empty() ? package.id : package.gid;
    const std::string name = package.name.empty() ? fallback : package.name;
    return scope_label.empty() ? name : scope_label + ": " + name;
}

std::vector<TerminologyPackageChoice> BuildTerminologyPackageChoices(const AppRuntimeState& state) {
    std::vector<TerminologyPackageChoice> choices;
    if (!state.app_state.sacm_package.has_value())
        return choices;

    const sacm::AssuranceCasePackage& package = state.app_state.sacm_package.value();
    for (const auto& terminology_package : package.terminologyPackages) {
        choices.push_back({TerminologyPackageRefFor(terminology_package),
                           PackageDisplayLabel(terminology_package, "Assurance case")});
    }
    for (const auto& argument_package : package.argumentPackages) {
        const std::string argument_label =
            argument_package.name.empty() ? (!argument_package.id.empty() ? argument_package.id : argument_package.gid)
                                          : argument_package.name;
        for (const auto& terminology_package : argument_package.terminologyPackages) {
            choices.push_back({TerminologyPackageRefFor(terminology_package),
                               PackageDisplayLabel(terminology_package, argument_label)});
        }
    }
    return choices;
}

int FindTerminologyPackageChoiceIndex(const std::vector<TerminologyPackageChoice>& choices,
                                      const core::TerminologyPackageRef& package_ref) {
    for (std::size_t index = 0; index < choices.size(); ++index) {
        if (SameTerminologyPackageRef(choices[index].ref, package_ref))
            return static_cast<int>(index);
    }
    return -1;
}

std::string PackageChoiceWidgetLabel(const TerminologyPackageChoice& choice, std::size_t index) {
    const std::string ref = !choice.ref.id.empty() ? choice.ref.id : choice.ref.gid;
    return choice.label + "##quick_define_target_package_" + ref + "_" + std::to_string(index);
}

} // namespace

class ModalHost {
public:
    ModalHost(AppRuntimeState& state, bool& done, const ModalHostCallbacks& callbacks)
        : state_(state), done_(done), callbacks_(callbacks) {}

    void Render();

private:
    void RenderPreferencesWindow();
    void RenderThemeTweaksWindow();
    void RenderNotImplementedModal();
    void RenderRemoveConfirmModal();
    void RenderDeleteReviewItemConfirmModal();
    void RenderStartupProjectWindow();
    void RenderCreateProjectModal();
    void RenderProjectFileNameModal();
    void RenderProjectLoadReportModal();
    void RenderSaveBeforeExitModal();
    void RenderCreateTerminologyPackageModal();
    void RenderDeleteTerminologyPackageModal();
    void RenderTerminologyTermEditorModal();
    void RenderQuickDefineTermModal();
    void RenderDeleteTerminologyTermModal();
    void RenderTerminologyCategoryEditorModal();
    void RenderDeleteTerminologyCategoryModal();
    void RenderReviewerNamePromptModal();

    AppRuntimeState& state_;
    bool& done_;
    const ModalHostCallbacks& callbacks_;
};

void ModalHost::Render() {
    RenderPreferencesWindow();
    RenderThemeTweaksWindow();
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

void ModalHost::RenderThemeTweaksWindow() {
    if (!state_.modal_coordinator->show_theme_tweak_window)
        return;
    HelloImGui::ShowThemeTweakGuiWindow(&state_.modal_coordinator->show_theme_tweak_window);
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
            state_.modal_coordinator->show_save_before_exit_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.modal_coordinator->show_save_before_exit_modal) {
        ImGui::OpenPopup("Unsaved Changes");
    }
}

void ModalHost::RenderCreateTerminologyPackageModal() {
    if (!state_.terminology.show_create_package_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create Terminology Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText(
            "Package name", state_.terminology.new_package_name_buf, sizeof(state_.terminology.new_package_name_buf));
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextMultiline("Package description",
                                  state_.terminology.new_package_description_buf,
                                  sizeof(state_.terminology.new_package_description_buf),
                                  ImVec2(420.0f, 96.0f));
        ImGui::Spacing();

        const bool can_create = !TrimWhitespace(state_.terminology.new_package_name_buf).empty();
        if (!can_create)
            ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_add_terminology_package();
            if (!state_.terminology.show_create_package_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_create)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_create_package_modal = false;
            state_.terminology.pending_package_parent_entry.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_create_package_modal) {
        ImGui::OpenPopup("Create Terminology Package");
    }
}

void ModalHost::RenderDeleteTerminologyPackageModal() {
    if (!state_.terminology.show_delete_package_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Terminology Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this terminology package?");
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_delete_terminology_package();
            if (!state_.terminology.show_delete_package_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_delete_package_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_delete_package_modal) {
        ImGui::OpenPopup("Delete Terminology Package");
    }
}

void ModalHost::RenderTerminologyTermEditorModal() {
    if (!state_.terminology.show_term_editor_modal)
        return;

    const char* title = state_.terminology.editing_existing_term ? "Edit Term" : "Create Term";
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        RenderTermTextFields(state_);
        RenderTermCategoryPicker(state_);
        RenderTermExternalReferenceField(state_);
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText("Origin", state_.terminology.term_origin_buf, sizeof(state_.terminology.term_origin_buf));

        const std::string value = TrimWhitespace(state_.terminology.term_value_buf);
        const std::string description = TrimWhitespace(state_.terminology.term_definition_buf);
        const bool can_save = !value.empty();
        RenderTerminologyTermValidationMessages(!can_save,
                                                false,
                                                CurrentTermDefinitionHasDuplicate(state_, value, description),
                                                description.empty(),
                                                TrimWhitespace(state_.terminology.term_categories_buf).empty());

        ImGui::Spacing();
        if (!can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button(state_.terminology.editing_existing_term ? "Save" : "Create", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_terminology_term_edit();
            if (!state_.terminology.show_term_editor_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_save)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_term_editor_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_term_editor_modal) {
        ImGui::OpenPopup(title);
    }
}

void ModalHost::RenderQuickDefineTermModal() {
    if (!state_.terminology.show_quick_define_term_modal)
        return;

    std::vector<TerminologyPackageChoice> package_choices = BuildTerminologyPackageChoices(state_);
    int selected_package_index =
        FindTerminologyPackageChoiceIndex(package_choices, state_.terminology.quick_define_target_package_ref);
    if (selected_package_index < 0 && !package_choices.empty()) {
        selected_package_index = 0;
        state_.terminology.quick_define_target_package_ref = package_choices.front().ref;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create Term##quick_define_term", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        RenderTermTextFields(state_);

        ImGui::TextUnformatted("Store in");
        if (package_choices.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "No TerminologyPackage is available.");
        } else {
            const char* preview = package_choices[static_cast<std::size_t>(selected_package_index)].label.c_str();
            ImGui::SetNextItemWidth(460.0f);
            if (ImGui::BeginCombo("##quick_define_target_package", preview)) {
                for (std::size_t index = 0; index < package_choices.size(); ++index) {
                    const bool selected = static_cast<int>(index) == selected_package_index;
                    const std::string selectable_label = PackageChoiceWidgetLabel(package_choices[index], index);
                    if (ImGui::Selectable(selectable_label.c_str(), selected)) {
                        selected_package_index = static_cast<int>(index);
                        state_.terminology.quick_define_target_package_ref = package_choices[index].ref;
                        CopyToBuffer(
                            state_.terminology.term_categories_buf, sizeof(state_.terminology.term_categories_buf), "");
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        RenderTermCategoryPickerForPackage(state_, state_.terminology.quick_define_target_package_ref);
        RenderTermExternalReferenceField(state_);

        const std::string value = TrimWhitespace(state_.terminology.term_value_buf);
        const std::string description = TrimWhitespace(state_.terminology.term_definition_buf);
        const bool has_target_package =
            HasTerminologyPackageRef(state_.terminology.quick_define_target_package_ref) &&
            state_.app_state.sacm_package.has_value() &&
            core::FindTerminologyPackage(state_.app_state.sacm_package.value(),
                                         state_.terminology.quick_define_target_package_ref);
        const bool can_create = !value.empty() && has_target_package;
        RenderTerminologyTermValidationMessages(
            value.empty(),
            !has_target_package,
            TermDefinitionHasDuplicate(
                state_, state_.terminology.quick_define_target_package_ref, value, description, false, {}),
            description.empty(),
            TrimWhitespace(state_.terminology.term_categories_buf).empty());

        ImGui::Spacing();
        if (!can_create)
            ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_quick_define_terminology_term(false);
            if (!state_.terminology.show_quick_define_term_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Create + Add as Context", ImVec2(185.0f, 0.0f))) {
            callbacks_.confirm_quick_define_terminology_term(true);
            if (!state_.terminology.show_quick_define_term_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_create)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_quick_define_term_modal = false;
            state_.terminology.quick_define_element_id.clear();
            state_.terminology.quick_define_source_text.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_quick_define_term_modal) {
        ImGui::OpenPopup("Create Term##quick_define_term");
    }
}

void ModalHost::RenderDeleteTerminologyTermModal() {
    if (!state_.terminology.show_delete_term_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Term", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this term?");
        if (state_.terminology.pending_delete_term_usage_count > 0) {
            ImGui::TextWrapped("This term value appears %d time(s) in the current SACM model.",
                               state_.terminology.pending_delete_term_usage_count);
        }
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_delete_terminology_term();
            if (!state_.terminology.show_delete_term_modal)
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_delete_term_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_delete_term_modal) {
        ImGui::OpenPopup("Delete Term");
    }
}

void ModalHost::RenderTerminologyCategoryEditorModal() {
    if (!state_.terminology.show_category_editor_modal)
        return;

    const char* title = state_.terminology.editing_existing_category ? "Edit Category" : "Create Category";
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText(
            "Category name", state_.terminology.category_name_buf, sizeof(state_.terminology.category_name_buf));
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextMultiline("Category description",
                                  state_.terminology.category_description_buf,
                                  sizeof(state_.terminology.category_description_buf),
                                  ImVec2(420.0f, 96.0f));

        const bool can_save = !TrimWhitespace(state_.terminology.category_name_buf).empty();
        if (!can_save)
            ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.2f, 1.0f), "Category name is required.");

        ImGui::Spacing();
        if (!can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button(state_.terminology.editing_existing_category ? "Save" : "Create", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_terminology_category_edit();
            if (!state_.terminology.show_category_editor_modal)
                ImGui::CloseCurrentPopup();
        }
        if (!can_save)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_category_editor_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_category_editor_modal) {
        ImGui::OpenPopup(title);
    }
}

void ModalHost::RenderDeleteTerminologyCategoryModal() {
    if (!state_.terminology.show_delete_category_modal)
        return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Category", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this category?");
        if (state_.terminology.pending_delete_category_term_count > 0) {
            ImGui::TextWrapped("This category is assigned to %d term(s). Remove those assignments before deleting it.",
                               state_.terminology.pending_delete_category_term_count);
        }
        ImGui::Spacing();
        if (state_.terminology.pending_delete_category_term_count > 0)
            ImGui::BeginDisabled();
        if (ImGui::Button("Delete", ImVec2(100.0f, 0.0f))) {
            callbacks_.confirm_delete_terminology_category();
            if (!state_.terminology.show_delete_category_modal)
                ImGui::CloseCurrentPopup();
        }
        if (state_.terminology.pending_delete_category_term_count > 0)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            state_.terminology.show_delete_category_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (state_.terminology.show_delete_category_modal) {
        ImGui::OpenPopup("Delete Category");
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
