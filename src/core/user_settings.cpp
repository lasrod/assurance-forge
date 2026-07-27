#include "core/user_settings.h"

#include <cstdlib>
#include <fstream>

namespace core {
namespace {

// Reads the whole settings document. Returns an empty object for anything we
// cannot use, so callers can treat "no file", "unreadable" and "malformed"
// identically -- each means there is nothing worth preserving.
nlohmann::json ReadSettingsDocument(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return nlohmann::json::object();
    }
    std::ifstream file(path);
    if (!file) {
        return nlohmann::json::object();
    }
    const nlohmann::json root =
        nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        return nlohmann::json::object();
    }
    return root;
}

} // namespace

std::filesystem::path UserSettingsFilePath() {
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

nlohmann::json ReadUserSettingsSection(const std::filesystem::path& path,
                                       const std::string&           section) {
    const nlohmann::json                 root  = ReadSettingsDocument(path);
    const nlohmann::json::const_iterator found = root.find(section);
    if (found == root.end()) {
        return nlohmann::json();
    }
    return *found;
}

bool UpdateUserSettingsSection(const std::filesystem::path& path, const std::string& section,
                               const nlohmann::json& value, std::string& error) {
    error.clear();
    try {
        // Read first, so sections owned by other components survive this write.
        nlohmann::json root = ReadSettingsDocument(path);
        root[section]       = value;

        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "Could not write settings to " + path.string() + ".";
            return false;
        }
        file << root.dump(2) << '\n';
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

McpUserSettings LoadMcpUserSettings(const std::filesystem::path& path) {
    McpUserSettings settings;

    const nlohmann::json section = ReadUserSettingsSection(path, "mcp");
    if (!section.is_object()) {
        return settings;
    }
    const nlohmann::json::const_iterator enabled = section.find("enabled");
    if (enabled == section.end() || !enabled->is_boolean()) {
        return settings;
    }
    settings.enabled = enabled->get<bool>();
    return settings;
}

bool SaveMcpUserSettings(const std::filesystem::path& path, const McpUserSettings& settings,
                         std::string& error) {
    return UpdateUserSettingsSection(path, "mcp", nlohmann::json{{"enabled", settings.enabled}},
                                     error);
}

} // namespace core
