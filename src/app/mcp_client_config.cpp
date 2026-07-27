#include "app/mcp_client_config.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace app {
namespace {

// Mirrors the discovery in guideline_catalog.cpp: the running executable's
// directory, falling back to the working directory where the platform gives us
// no better answer.
std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    char        path[MAX_PATH] = {};
    const DWORD length         = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(path).parent_path();
    }
#endif
    std::error_code ec;
    const std::filesystem::path working = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{} : working;
}

const char* ServerExecutableName() {
#ifdef _WIN32
    return "assurance-forge-mcp.exe";
#else
    return "assurance-forge-mcp";
#endif
}

} // namespace

std::filesystem::path McpServerExecutablePath() {
    const std::filesystem::path directory = ExecutableDirectory();
    if (directory.empty()) {
        return {};
    }
    return directory / ServerExecutableName();
}

std::string BuildMcpClientConfig(const std::filesystem::path& project_root) {
    const std::filesystem::path server = McpServerExecutablePath();

    std::error_code ec;
    if (server.empty() || !std::filesystem::exists(server, ec)) {
        return {};
    }

    // generic_string() so Windows paths use forward slashes. Backslashes would
    // need escaping in JSON, and a config a user hand-edits later is one more
    // place for a stray escape to break the launch.
    const nlohmann::json config{
        {"mcpServers",
         {{"assurance-forge",
           {{"command", server.generic_string()},
            {"args", nlohmann::json::array({"--project", project_root.generic_string()})}}}}}};

    return config.dump(2);
}

} // namespace app
