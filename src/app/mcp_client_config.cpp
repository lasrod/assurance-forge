#include "app/mcp_client_config.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace app {
namespace {

// Full path of the running executable, or empty when the platform cannot say.
//
// One definition per platform rather than one function full of #if branches.
// The branching version put each platform's locals in the *same* scope as the
// shared fallback's, so a variable named the same in both compiled on Windows
// and failed to compile on Linux -- a whole class of error that cannot happen
// when each platform gets its own function body.
#ifdef _WIN32
std::filesystem::path RunningExecutablePath() {
    char        path[MAX_PATH] = {};
    const DWORD length         = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(path);
}
#elif defined(__APPLE__)
std::filesystem::path RunningExecutablePath() {
    // Never called with a null buffer: a too-small buffer returns non-zero and
    // writes the required size back, so one grow-and-retry is enough.
    std::vector<char> buffer(1024, '\0');
    std::uint32_t     size = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.assign(static_cast<std::size_t>(size) + 1, '\0');
        size = static_cast<std::uint32_t>(buffer.size());
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }
    }
    std::error_code             ec;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(buffer.data(), ec);
    return ec ? std::filesystem::path(buffer.data()) : resolved;
}
#elif defined(__linux__)
std::filesystem::path RunningExecutablePath() {
    std::error_code             ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : self;
}
#else
std::filesystem::path RunningExecutablePath() {
    return {};
}
#endif

// The directory the running executable lives in.
//
// The working-directory fallback is genuinely a last resort, and was actively
// wrong when it was the only answer off Windows: the test harness runs with
// `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}`, so on Linux and macOS this resolved
// to the *source* tree, found no server binary beside it, and failed the
// client-config tests on both. It would equally have told a user "server not
// found" whenever the app was launched from anywhere but its own directory.
std::filesystem::path ExecutableDirectory() {
    const std::filesystem::path self = RunningExecutablePath();
    if (!self.empty()) {
        return self.parent_path();
    }
    std::error_code             ec;
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
