#include "ui/panels/preferences_panel.h"

#include "imgui.h"
#include "ui/localization.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstring>

namespace ui::panels {
namespace {

ImVec4 StatusColor(const ai::AiConnectionStatus& status) {
    const ui::Theme& theme = ui::GetTheme();
    if (status.state == ai::AiTaskState::Running) return ImGui::ColorConvertU32ToFloat4(theme.warning);
    if (status.state == ai::AiTaskState::Success) return ImGui::ColorConvertU32ToFloat4(theme.success);
    if (status.state == ai::AiTaskState::Error || status.errorCode != ai::AiErrorCode::None) return ImGui::ColorConvertU32ToFloat4(theme.danger);
    return ImGui::ColorConvertU32ToFloat4(theme.text_secondary);
}

void SyncModelBuffer(PreferencesPanelModel& model) {
    if (!model.settings || !model.modelBuffer || model.modelBufferSize == 0) return;
    size_t count = std::min(model.modelBufferSize - 1, model.settings->model.size());
    std::memcpy(model.modelBuffer, model.settings->model.data(), count);
    model.modelBuffer[count] = '\0';
}

void RenderConnectionStatus(const ai::AiConnectionStatus& status) {
    if (status.message.empty()) return;

    ImVec4 color = StatusColor(status);
    if (status.state == ai::AiTaskState::Running || status.state == ai::AiTaskState::Success) {
        ImGui::SameLine();
        ImGui::TextColored(color, "%s", status.message.c_str());
        return;
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", status.message.c_str());
    ImGui::PopStyleColor();
}

void RenderAiSection(PreferencesPanelModel model, const PreferencesPanelCallbacks& callbacks) {
    if (!model.settings) {
        ImGui::TextDisabled("%s", ui::Tr(ui::MessageId::AiSettingsUnavailable));
        return;
    }

    ai::AiProviderSettings draft = *model.settings;

    ImGui::TextUnformatted(ui::Tr(ui::MessageId::Ai));
    ImGui::Separator();

    bool enabled = draft.enabled;
    if (ImGui::Checkbox(ui::Tr(ui::MessageId::EnableAiSupport), &enabled)) {
        draft.enabled = enabled;
        *model.settings = draft;
    }

    ImGui::TextUnformatted(ui::Tr(ui::MessageId::Provider));
    ImGui::SetNextItemWidth(220.0f);
    ImGui::BeginDisabled();
    static char provider_name[] = "OpenAI";
    ImGui::InputText("##ai_provider", provider_name, sizeof(provider_name),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();

    ImGui::TextUnformatted(ui::Tr(ui::MessageId::Model));
    ImGui::SetNextItemWidth(280.0f);
    if (model.modelBuffer && model.modelBufferSize > 0 &&
        ImGui::InputText("##ai_model", model.modelBuffer, model.modelBufferSize)) {
        model.settings->model = model.modelBuffer;
    }

    if (ImGui::Button(ui::Tr(ui::MessageId::SaveAiSettings))) {
        if (model.modelBuffer && model.modelBufferSize > 0) {
            model.settings->model = model.modelBuffer;
        }
        if (callbacks.save_settings) callbacks.save_settings(*model.settings);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(ui::Tr(ui::MessageId::ApiKey));
    if (!model.secureStoreAvailable) {
        ImGui::TextColored(StatusColor(ai::ErrorStatus(ai::AiErrorCode::SecureStoreUnavailable, "")),
                           "%s", ui::Tr(ui::MessageId::SecureStorageUnavailable));
    } else if (model.keyStored) {
        ImGui::TextDisabled("%s", ui::Tr(ui::MessageId::KeyStored));
    } else {
        ImGui::TextDisabled("%s", ui::Tr(ui::MessageId::NoApiKeyStored));
    }

    ImGuiInputTextFlags key_flags = ImGuiInputTextFlags_Password;
    ImGui::SetNextItemWidth(360.0f);
    if (model.apiKeyBuffer && model.apiKeyBufferSize > 0) {
        ImGui::InputText("##openai_key", model.apiKeyBuffer, model.apiKeyBufferSize, key_flags);
    }

    if (!model.secureStoreAvailable) ImGui::BeginDisabled();
    if (ImGui::Button(model.keyStored ? ui::Tr(ui::MessageId::UpdateKey) : ui::Tr(ui::MessageId::SaveKey))) {
        if (callbacks.save_api_key && model.apiKeyBuffer) callbacks.save_api_key(model.apiKeyBuffer);
    }
    ImGui::SameLine();
    if (!model.keyStored) ImGui::BeginDisabled();
    if (ImGui::Button(ui::Tr(ui::MessageId::RemoveKey))) {
        if (callbacks.remove_api_key) callbacks.remove_api_key();
    }
    if (!model.keyStored) ImGui::EndDisabled();
    if (!model.secureStoreAvailable) ImGui::EndDisabled();

    ImGui::Spacing();
    const bool can_test = model.secureStoreAvailable && model.keyStored && model.settings->enabled && !model.testRunning;
    if (!can_test) ImGui::BeginDisabled();
    if (ImGui::Button(ui::Tr(ui::MessageId::TestConnection))) {
        if (callbacks.test_connection) callbacks.test_connection();
    }
    if (!can_test) ImGui::EndDisabled();

    RenderConnectionStatus(model.connectionStatus);

    ImGui::Spacing();
    ImGui::TextWrapped("%s", ui::Tr(ui::MessageId::AiPrivacyNotice));
}

void RenderAppearanceSection(PreferencesPanelModel model, const PreferencesPanelCallbacks& callbacks) {
    ImGui::TextUnformatted(ui::Tr(ui::MessageId::Appearance));
    ImGui::Separator();

    ImGui::TextUnformatted(ui::Tr(ui::MessageId::Language));
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##language", ui::LanguageDisplayName(model.language))) {
        const ui::Language languages[] = {ui::Language::English, ui::Language::Japanese};
        for (ui::Language language : languages) {
            const bool selected = model.language == language;
            if (ImGui::Selectable(ui::LanguageDisplayName(language), selected)) {
                if (callbacks.set_language) callbacks.set_language(language);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

}  // namespace

void ShowPreferencesWindow(bool& open,
                           PreferencesPanelModel model,
                           const PreferencesPanelCallbacks& callbacks) {
    if (!open) return;

    SyncModelBuffer(model);
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::Begin(ui::Tr(ui::MessageId::PreferencesTitle), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        RenderAppearanceSection(model, callbacks);
        ImGui::Spacing();
        RenderAiSection(model, callbacks);
    }
    ImGui::End();
}

}  // namespace ui::panels