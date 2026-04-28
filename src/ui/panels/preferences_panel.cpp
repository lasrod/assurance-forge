#include "ui/panels/preferences_panel.h"

#include "imgui.h"
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
        ImGui::TextDisabled("AI settings are unavailable.");
        return;
    }

    ai::AiProviderSettings draft = *model.settings;

    ImGui::TextUnformatted("AI");
    ImGui::Separator();

    bool enabled = draft.enabled;
    if (ImGui::Checkbox("Enable AI support", &enabled)) {
        draft.enabled = enabled;
        *model.settings = draft;
    }

    ImGui::TextUnformatted("Provider");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::BeginDisabled();
    static char provider_name[] = "OpenAI";
    ImGui::InputText("##ai_provider", provider_name, sizeof(provider_name),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();

    ImGui::TextUnformatted("Model");
    ImGui::SetNextItemWidth(280.0f);
    if (model.modelBuffer && model.modelBufferSize > 0 &&
        ImGui::InputText("##ai_model", model.modelBuffer, model.modelBufferSize)) {
        model.settings->model = model.modelBuffer;
    }

    if (ImGui::Button("Save AI Settings")) {
        if (model.modelBuffer && model.modelBufferSize > 0) {
            model.settings->model = model.modelBuffer;
        }
        if (callbacks.save_settings) callbacks.save_settings(*model.settings);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("API Key");
    if (!model.secureStoreAvailable) {
        ImGui::TextColored(StatusColor(ai::ErrorStatus(ai::AiErrorCode::SecureStoreUnavailable, "")),
                           "Secure storage is unavailable on this platform.");
    } else if (model.keyStored) {
        ImGui::TextDisabled("Key stored: ********");
    } else {
        ImGui::TextDisabled("No API key stored.");
    }

    ImGuiInputTextFlags key_flags = ImGuiInputTextFlags_Password;
    ImGui::SetNextItemWidth(360.0f);
    if (model.apiKeyBuffer && model.apiKeyBufferSize > 0) {
        ImGui::InputText("##openai_key", model.apiKeyBuffer, model.apiKeyBufferSize, key_flags);
    }

    if (!model.secureStoreAvailable) ImGui::BeginDisabled();
    if (ImGui::Button(model.keyStored ? "Update Key" : "Save Key")) {
        if (callbacks.save_api_key && model.apiKeyBuffer) callbacks.save_api_key(model.apiKeyBuffer);
    }
    ImGui::SameLine();
    if (!model.keyStored) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Key")) {
        if (callbacks.remove_api_key) callbacks.remove_api_key();
    }
    if (!model.keyStored) ImGui::EndDisabled();
    if (!model.secureStoreAvailable) ImGui::EndDisabled();

    ImGui::Spacing();
    const bool can_test = model.secureStoreAvailable && model.keyStored && model.settings->enabled && !model.testRunning;
    if (!can_test) ImGui::BeginDisabled();
    if (ImGui::Button("Test Connection")) {
        if (callbacks.test_connection) callbacks.test_connection();
    }
    if (!can_test) ImGui::EndDisabled();

    RenderConnectionStatus(model.connectionStatus);

    ImGui::Spacing();
    ImGui::TextWrapped("AI features may send selected safety case content and prompts to the configured AI provider. Assurance Forge will not send project data automatically; data is sent only when you explicitly use an AI action.");
}

}  // namespace

void ShowPreferencesWindow(bool& open,
                           PreferencesPanelModel model,
                           const PreferencesPanelCallbacks& callbacks) {
    if (!open) return;

    SyncModelBuffer(model);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::Begin("Preferences", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        RenderAiSection(model, callbacks);
    }
    ImGui::End();
}

}  // namespace ui::panels