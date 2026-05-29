#include "ui/panels/preferences_panel.h"

#include "imgui.h"
#include "ui/gsn/gsn_dpi.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstring>

namespace ui::panels {
namespace {

ImVec4 StatusColor(const ai::AiConnectionStatus& status) {
    const ui::Theme& theme = ui::GetTheme();
    if (status.state == ai::AiTaskState::Running)
        return ImGui::ColorConvertU32ToFloat4(theme.warning);
    if (status.state == ai::AiTaskState::Success)
        return ImGui::ColorConvertU32ToFloat4(theme.success);
    if (status.state == ai::AiTaskState::Error || status.errorCode != ai::AiErrorCode::None)
        return ImGui::ColorConvertU32ToFloat4(theme.danger);
    return ImGui::ColorConvertU32ToFloat4(theme.text_secondary);
}

void SyncModelBuffer(PreferencesPanelModel& model) {
    if (!model.settings || !model.modelBuffer || model.modelBufferSize == 0)
        return;
    size_t count = std::min(model.modelBufferSize - 1, model.settings->model.size());
    std::memcpy(model.modelBuffer, model.settings->model.data(), count);
    model.modelBuffer[count] = '\0';
}

void RenderConnectionStatus(const ai::AiConnectionStatus& status) {
    if (status.message.empty())
        return;

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
        ImGui::TextDisabled("%s", AF_TR("AI settings are unavailable.").c_str());
        return;
    }

    ai::AiProviderSettings draft = *model.settings;

    ImGui::TextUnformatted(AF_TR("AI").c_str());
    ImGui::Separator();

    bool enabled = draft.enabled;
    if (ImGui::Checkbox(AF_TR("Enable AI support").c_str(), &enabled)) {
        draft.enabled = enabled;
        *model.settings = draft;
    }

    ImGui::TextUnformatted(AF_TR("Provider").c_str());
    ImGui::SetNextItemWidth(220.0f);
    ImGui::BeginDisabled();
    static char provider_name[] = "OpenAI";
    ImGui::InputText("##ai_provider", provider_name, sizeof(provider_name), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();

    ImGui::TextUnformatted(AF_TR("Model").c_str());
    ImGui::SetNextItemWidth(280.0f);
    if (model.modelBuffer && model.modelBufferSize > 0 &&
        ImGui::InputText("##ai_model", model.modelBuffer, model.modelBufferSize)) {
        model.settings->model = model.modelBuffer;
    }

    if (ImGui::Button(AF_TR("Save AI Settings").c_str())) {
        if (model.modelBuffer && model.modelBufferSize > 0) {
            model.settings->model = model.modelBuffer;
        }
        if (callbacks.save_settings)
            callbacks.save_settings(*model.settings);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(AF_TR("API Key").c_str());
    if (!model.secureStoreAvailable) {
        ImGui::TextColored(StatusColor(ai::ErrorStatus(ai::AiErrorCode::SecureStoreUnavailable, "")),
                           "%s",
                           AF_TR("Secure storage is unavailable on this platform.").c_str());
    } else if (model.keyStored) {
        ImGui::TextDisabled("%s", AF_TR("Key stored: ********").c_str());
    } else {
        ImGui::TextDisabled("%s", AF_TR("No API key stored.").c_str());
    }

    ImGuiInputTextFlags key_flags = ImGuiInputTextFlags_Password;
    ImGui::SetNextItemWidth(360.0f);
    if (model.apiKeyBuffer && model.apiKeyBufferSize > 0) {
        ImGui::InputText("##openai_key", model.apiKeyBuffer, model.apiKeyBufferSize, key_flags);
    }

    if (!model.secureStoreAvailable)
        ImGui::BeginDisabled();
    if (ImGui::Button((model.keyStored ? AF_TR("Update Key") : AF_TR("Save Key")).c_str())) {
        if (callbacks.save_api_key && model.apiKeyBuffer)
            callbacks.save_api_key(model.apiKeyBuffer);
    }
    ImGui::SameLine();
    if (!model.keyStored)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Remove Key").c_str())) {
        if (callbacks.remove_api_key)
            callbacks.remove_api_key();
    }
    if (!model.keyStored)
        ImGui::EndDisabled();
    if (!model.secureStoreAvailable)
        ImGui::EndDisabled();

    ImGui::Spacing();
    const bool can_test =
        model.secureStoreAvailable && model.keyStored && model.settings->enabled && !model.testRunning;
    if (!can_test)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Test Connection").c_str())) {
        if (callbacks.test_connection)
            callbacks.test_connection();
    }
    if (!can_test)
        ImGui::EndDisabled();

    RenderConnectionStatus(model.connectionStatus);

    ImGui::Spacing();
    ImGui::TextWrapped(
        "%s",
        AF_TR("AI features may send selected safety case content and prompts to the configured AI provider. Assurance "
              "Forge will not send project data automatically; data is sent only when you explicitly use an AI action.")
            .c_str());
}

void RenderAppearanceSection(PreferencesPanelModel model, const PreferencesPanelCallbacks& callbacks) {
    ImGui::TextUnformatted(AF_TR("Appearance").c_str());
    ImGui::Separator();
    const float combo_width = ui::gsn::DpiSize(220.0f);

    ImGui::TextUnformatted(AF_TR("Theme").c_str());
    ImGui::SetNextItemWidth(combo_width);
    if (ImGui::BeginCombo("##theme", AF_TR(ui::GetThemeDisplayName(model.theme)).c_str())) {
        for (ui::AppTheme theme : ui::kAppThemes) {
            const bool selected = model.theme == theme;
            if (ImGui::Selectable(AF_TR(ui::GetThemeDisplayName(theme)).c_str(), selected)) {
                if (callbacks.set_theme)
                    callbacks.set_theme(theme);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::TextUnformatted(AF_TR("Language").c_str());
    ImGui::SetNextItemWidth(combo_width);
    if (ImGui::BeginCombo("##language", ui::i18n::LanguageDisplayName(model.language).c_str())) {
        const ui::i18n::Language languages[] = {ui::i18n::Language::English, ui::i18n::Language::Japanese};
        for (ui::i18n::Language language : languages) {
            const bool selected = model.language == language;
            if (ImGui::Selectable(ui::i18n::LanguageDisplayName(language).c_str(), selected)) {
                if (callbacks.set_language)
                    callbacks.set_language(language);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    bool show_fps = model.showFps;
    if (ImGui::Checkbox(AF_TR("Show FPS").c_str(), &show_fps)) {
        if (callbacks.set_show_fps)
            callbacks.set_show_fps(show_fps);
    }
    ImGui::TextColored(ui::CullRatioColor(0.8f), "%s", AF_TR("High culling").c_str());
    ImGui::SameLine();
    ImGui::TextColored(ui::CullRatioColor(0.5f), "%s", AF_TR("Medium culling").c_str());
    ImGui::SameLine();
    ImGui::TextColored(ui::CullRatioColor(0.0f), "%s", AF_TR("Low culling").c_str());
}

void RenderReviewSection(PreferencesPanelModel model, const PreferencesPanelCallbacks& callbacks) {
    ImGui::TextUnformatted(AF_TR("Review").c_str());
    ImGui::Separator();

    ImGui::TextUnformatted(AF_TR("Reviewer name").c_str());
    ImGui::SetNextItemWidth(320.0f);
    if (model.reviewerNameBuffer && model.reviewerNameBufferSize > 0) {
        ImGui::InputText("##reviewer_name", model.reviewerNameBuffer, model.reviewerNameBufferSize);
    }

    const bool can_save = model.reviewerNameBuffer && model.reviewerNameBuffer[0] != '\0';
    if (!can_save)
        ImGui::BeginDisabled();
    if (ImGui::Button(AF_TR("Save Reviewer Name").c_str()) && callbacks.save_reviewer_name &&
        model.reviewerNameBuffer) {
        callbacks.save_reviewer_name(model.reviewerNameBuffer);
    }
    if (!can_save)
        ImGui::EndDisabled();
}

} // namespace

void ShowPreferencesWindow(bool& open, PreferencesPanelModel model, const PreferencesPanelCallbacks& callbacks) {
    if (!open)
        return;

    SyncModelBuffer(model);
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::Begin(AF_TR("Preferences").c_str(),
                     &open,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        RenderAppearanceSection(model, callbacks);
        ImGui::Spacing();
        RenderReviewSection(model, callbacks);
        ImGui::Spacing();
        RenderAiSection(model, callbacks);
    }
    ImGui::End();
}

} // namespace ui::panels
