#include "core/user_settings.h"

#include <cstdlib>

namespace core {

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

} // namespace core
