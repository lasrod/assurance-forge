#pragma once

#include "ui/i18n/language.h"
#include "ui/theme.h"

#include <cstddef>
#include <functional>
#include <string>

namespace ui::panels {

// How an AI connection attempt is going, in the only terms the panel needs:
// which colour to draw and whether a spinner belongs on screen.
//
// Deliberately not `ai::AiConnectionStatus`. Naming an `ai` type here would make
// `ui` depend on `ai`, which the layer gate forbids and which was one of its two
// recorded exceptions. The panel renders a severity; deciding what an
// `AiErrorCode` means is `app`'s job, not the renderer's.
enum class AiStatusSeverity {
    Idle,
    Running,
    Success,
    Error,
};

struct PreferencesPanelModel {
    // AI settings, as plain data. `aiAvailable` is false when there is nothing
    // to edit, which the panel shows instead of an empty form.
    bool aiAvailable = false;
    bool aiEnabled = false;
    // Read-only provider name. One provider is supported today.
    std::string aiProviderName;
    bool keyStored = false;
    bool secureStoreAvailable = false;
    bool testRunning = false;
    AiStatusSeverity connectionSeverity = AiStatusSeverity::Idle;
    std::string connectionMessage;
    char* apiKeyBuffer = nullptr;
    size_t apiKeyBufferSize = 0;
    char* modelBuffer = nullptr;
    size_t modelBufferSize = 0;
    char* reviewerNameBuffer = nullptr;
    size_t reviewerNameBufferSize = 0;
    ui::AppTheme theme = ui::AppTheme::Dark;
    ui::i18n::Language language = ui::i18n::Language::English;
    bool showDeveloperTools = false;

    // MCP server. The panel receives plain data rather than reaching into the
    // `mcp` layer, which it must not depend on.
    bool mcpEnabled = false;
    // Client configuration to copy, already rendered. Empty when it cannot be
    // built, in which case `mcpConfigUnavailableReason` says why.
    std::string mcpClientConfig;
    std::string mcpConfigUnavailableReason;
    // Why the last toggle did not persist. Empty when nothing went wrong.
    // Without this the checkbox flips, silently fails to save, and reverts on
    // the next frame with nothing to explain it.
    std::string mcpStatus;
};

struct PreferencesPanelCallbacks {
    // Enabled state and model text. `app` owns the rest of the settings record.
    std::function<void(bool enabled, const char* model)> save_settings;
    std::function<void(bool enabled)> set_ai_enabled;
    std::function<void(const char*)> save_api_key;
    std::function<void()> remove_api_key;
    std::function<void()> test_connection;
    std::function<void(ui::AppTheme)> set_theme;
    std::function<void(ui::i18n::Language)> set_language;
    std::function<void(bool)> set_show_developer_tools;
    std::function<void(const char*)> save_reviewer_name;
    std::function<void(bool)> set_mcp_enabled;
};

void ShowPreferencesWindow(bool& open, PreferencesPanelModel model, const PreferencesPanelCallbacks& callbacks);

} // namespace ui::panels