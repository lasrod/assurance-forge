#include "app/mcp_client_config.h"

#include "app/executable_location.h"
#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

namespace app {
namespace {

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
    const nlohmann::json config{{"mcpServers",
                                 {{"assurance-forge",
                                   {{"command", server.generic_string()},
                                    {"args", nlohmann::json::array({"--project", project_root.generic_string()})}}}}}};

    return config.dump(2);
}

} // namespace app
