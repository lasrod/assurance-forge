#include "core/active_argument.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace core {
namespace {

constexpr const char* kActiveArgumentKey = "activeArgument";

} // namespace

std::filesystem::path ActiveArgumentFilePath(const std::filesystem::path& project_root) {
    return project_root / ".af-session.json";
}

std::string ReadActiveArgument(const std::filesystem::path& project_root) {
    const std::filesystem::path path = ActiveArgumentFilePath(project_root);

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {};
    }
    std::ifstream file(path);
    if (!file) {
        return {};
    }

    const nlohmann::json root =
        nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        return {};
    }
    const nlohmann::json::const_iterator active = root.find(kActiveArgumentKey);
    if (active == root.end() || !active->is_string()) {
        return {};
    }
    return active->get<std::string>();
}

bool WriteActiveArgument(const std::filesystem::path& project_root,
                         const std::filesystem::path& relative_path, std::string& error) {
    error.clear();
    try {
        const std::filesystem::path path = ActiveArgumentFilePath(project_root);
        std::filesystem::create_directories(project_root);

        const nlohmann::json root{{kActiveArgumentKey, relative_path.generic_string()}};
        std::ofstream        file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "Could not write " + path.string();
            return false;
        }
        file << root.dump(2) << '\n';
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace core
