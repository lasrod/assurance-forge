#include "ai/ai_settings.h"

#include "core/user_settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace ai {
namespace {

AiProviderSettings DefaultSettings() {
    AiProviderSettings settings;
    settings.provider = AiProviderId::OpenAI;
    settings.displayName = kOpenAiProviderName;
    settings.model = kDefaultOpenAiModel;
    settings.enabled = false;
    settings.sendProjectDataOnlyOnExplicitUserAction = true;
    return settings;
}

bool JsonBool(const nlohmann::json& object, const char* key, bool fallback) {
    auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_boolean())
        return fallback;
    return iterator->get<bool>();
}

std::string JsonString(const nlohmann::json& object, const char* key, const std::string& fallback) {
    auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_string())
        return fallback;
    return iterator->get<std::string>();
}

} // namespace

AiSettingsStore::AiSettingsStore() : settings_path_(DefaultSettingsPath()) {}

AiSettingsStore::AiSettingsStore(std::filesystem::path settings_path) : settings_path_(std::move(settings_path)) {}

std::filesystem::path AiSettingsStore::DefaultSettingsPath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        return std::filesystem::path(appdata) / "AssuranceForge" / "settings.json";
    }
#else
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        return std::filesystem::path(xdg_config) / "assurance-forge" / "settings.json";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".config" / "assurance-forge" / "settings.json";
    }
#endif
    return std::filesystem::temp_directory_path() / "AssuranceForge" / "settings.json";
}

AiProviderSettings AiSettingsStore::Load(std::string* warning) const {
    AiProviderSettings settings = DefaultSettings();
    if (warning)
        warning->clear();
    if (!std::filesystem::exists(settings_path_))
        return settings;

    try {
        std::ifstream file(settings_path_);
        if (!file) {
            if (warning)
                *warning = "Could not open AI settings.";
            return settings;
        }

        nlohmann::json root = nlohmann::json::parse(file, nullptr, true, true);
        if (!root.is_object())
            return settings;
        nlohmann::json ai = root.value("ai", nlohmann::json::object());
        if (!ai.is_object())
            return settings;

        settings.enabled = JsonBool(ai, "enabled", settings.enabled);
        settings.provider = AiProviderIdFromString(JsonString(ai, "provider", kOpenAiProviderId));
        settings.displayName = ToString(settings.provider);
        settings.model = JsonString(ai, "model", settings.model);
        if (settings.model.empty())
            settings.model = kDefaultOpenAiModel;
        settings.sendProjectDataOnlyOnExplicitUserAction =
            JsonBool(ai, "sendProjectDataOnlyOnExplicitUserAction", settings.sendProjectDataOnlyOnExplicitUserAction);
    } catch (const std::exception& exception) {
        if (warning)
            *warning = std::string("AI settings were reset to defaults: ") + exception.what();
        return DefaultSettings();
    }

    return settings;
}

bool AiSettingsStore::Save(const AiProviderSettings& settings, std::string& error) const {
    // Writes only the "ai" section, read-modify-write, so sibling sections
    // survive. This used to build a fresh document and truncate the file, which
    // silently deleted the "mcp" section on every save and turned the MCP server
    // off with no diagnostic. See core/user_settings.h.
    const nlohmann::json ai{
        {"enabled", settings.enabled},
        {"provider", ToSettingsString(settings.provider)},
        {"model", settings.model.empty() ? kDefaultOpenAiModel : settings.model},
        {"sendProjectDataOnlyOnExplicitUserAction", settings.sendProjectDataOnlyOnExplicitUserAction},
    };
    return core::UpdateUserSettingsSection(settings_path_, "ai", ai, error);
}

} // namespace ai
