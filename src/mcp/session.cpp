#include "mcp/session.h"

#include "core/user_settings.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>

namespace mcp {
namespace {

// A path we should hand to AppState::open_project rather than load_file: a
// directory, or a manifest. Bare SACM files load standalone, which is useful for
// inspecting a file that is not part of a project, but yields no project root.
bool LooksLikeAssuranceCaseFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension == ".sacm" || extension == ".xml";
}

} // namespace

bool ReadMcpConsent(const std::filesystem::path& settings_path) {
    std::error_code ec;
    if (!std::filesystem::exists(settings_path, ec)) {
        return false;
    }

    std::ifstream file(settings_path);
    if (!file) {
        return false;
    }

    // Comments are tolerated because the sibling "ai" section is read the same
    // way, and a hand-edited settings file is the only way to enable MCP until
    // the preferences toggle lands.
    const nlohmann::json root =
        nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    const nlohmann::json::const_iterator section = root.find("mcp");
    if (section == root.end() || !section->is_object()) {
        return false;
    }

    const nlohmann::json::const_iterator enabled = section->find("enabled");
    if (enabled == section->end() || !enabled->is_boolean()) {
        return false;
    }
    return enabled->get<bool>();
}

std::unique_ptr<Session> Session::Open(Config config, std::string& error) {
    error.clear();

    if (config.project_path.empty()) {
        error = "No project path was given. Pass --project <path>.";
        return nullptr;
    }

    std::error_code ec;
    if (!std::filesystem::exists(config.project_path, ec)) {
        error = "Project path does not exist: " + config.project_path.string();
        return nullptr;
    }

    std::unique_ptr<Session> session(new Session());
    session->project_path_ = config.project_path;
    session->consent_granted_ =
        ReadMcpConsent(config.settings_path.empty() ? core::UserSettingsFilePath()
                                                    : config.settings_path);

    const bool is_directory = std::filesystem::is_directory(config.project_path, ec);
    const bool opened = (is_directory || !LooksLikeAssuranceCaseFile(config.project_path))
                            ? session->state_.open_project(config.project_path.string())
                            : session->state_.load_file(config.project_path.string());

    if (!opened) {
        error = session->state_.status_message.empty()
                    ? ("Could not open " + config.project_path.string())
                    : session->state_.status_message;
        return nullptr;
    }

    return session;
}

} // namespace mcp
