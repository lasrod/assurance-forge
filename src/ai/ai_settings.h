#pragma once

#include "ai/ai_types.h"

#include <filesystem>
#include <string>

namespace ai {

class AiSettingsStore {
public:
    AiSettingsStore();
    explicit AiSettingsStore(std::filesystem::path settings_path);

    AiProviderSettings Load(std::string* warning = nullptr) const;
    bool Save(const AiProviderSettings& settings, std::string& error) const;

    const std::filesystem::path& SettingsPath() const { return settings_path_; }
    static std::filesystem::path DefaultSettingsPath();

private:
    std::filesystem::path settings_path_;
};

}  // namespace ai
