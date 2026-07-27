#pragma once

#include "ai/ai_types.h"
#include "ui/i18n/language.h"
#include "ui/theme.h"

#include <cstddef>
#include <functional>
#include <string>

namespace ui::panels {

struct PreferencesPanelModel {
    ai::AiProviderSettings* settings = nullptr;
    bool keyStored = false;
    bool secureStoreAvailable = false;
    bool testRunning = false;
    ai::AiConnectionStatus connectionStatus;
    char* apiKeyBuffer = nullptr;
    size_t apiKeyBufferSize = 0;
    char* modelBuffer = nullptr;
    size_t modelBufferSize = 0;
    char* reviewerNameBuffer = nullptr;
    size_t reviewerNameBufferSize = 0;
    ui::AppTheme theme = ui::AppTheme::Dark;
    ui::i18n::Language language = ui::i18n::Language::English;
    bool showFps = false;

    // MCP server. The panel receives plain data rather than reaching into the
    // `mcp` layer, which it must not depend on.
    bool mcpEnabled = false;
    // Client configuration to copy, already rendered. Empty when it cannot be
    // built, in which case `mcpConfigUnavailableReason` says why.
    std::string mcpClientConfig;
    std::string mcpConfigUnavailableReason;
};

struct PreferencesPanelCallbacks {
    std::function<void(const ai::AiProviderSettings&)> save_settings;
    std::function<void(const char*)> save_api_key;
    std::function<void()> remove_api_key;
    std::function<void()> test_connection;
    std::function<void(ui::AppTheme)> set_theme;
    std::function<void(ui::i18n::Language)> set_language;
    std::function<void(bool)> set_show_fps;
    std::function<void(const char*)> save_reviewer_name;
    std::function<void(bool)> set_mcp_enabled;
};

void ShowPreferencesWindow(bool& open, PreferencesPanelModel model, const PreferencesPanelCallbacks& callbacks);

} // namespace ui::panels