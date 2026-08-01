#include "app/mcp_client_config.h"

#include "core/user_settings.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

// The configuration block Preferences offers to copy. It exists so enabling MCP
// does not mean hand-writing JSON with two absolute paths in it, so the shape has
// to be right: a client accepts a malformed or wrongly-pathed config and then
// fails somewhere the user is not looking.
//
// CMake makes `tests` depend on `assurance-forge-mcp`, so the binary really is
// beside the test executable and these exercise the discovery rather than the
// not-found branch.

namespace {

nlohmann::json ParsedConfig(const std::string& config) {
    return nlohmann::json::parse(config, nullptr, /*allow_exceptions=*/false);
}

} // namespace

TEST(McpClientConfig, NamesTheServerBinaryAndTheRequestedProject) {
    const std::string config = app::BuildMcpClientConfig("C:/cases/MyCase");
    ASSERT_FALSE(config.empty()) << "expected assurance-forge-mcp beside the test executable at "
                                 << app::McpServerExecutablePath().string();

    const nlohmann::json parsed = ParsedConfig(config);
    ASSERT_FALSE(parsed.is_discarded()) << config;

    const nlohmann::json& entry = parsed["mcpServers"]["assurance-forge"];
    EXPECT_NE(entry["command"].get<std::string>().find("assurance-forge-mcp"), std::string::npos);
    ASSERT_EQ(entry["args"].size(), 2u);
    EXPECT_EQ(entry["args"][0], "--project");
    EXPECT_EQ(entry["args"][1], "C:/cases/MyCase");
}

// Windows paths would arrive with backslashes, which JSON escapes. A user who
// later hand-edits the block is then one stray escape away from a launch that
// fails for a reason that looks nothing like the cause.
TEST(McpClientConfig, UsesForwardSlashesSoNothingNeedsEscaping) {
    const std::string config = app::BuildMcpClientConfig("C:/cases/MyCase");
    ASSERT_FALSE(config.empty());
    EXPECT_EQ(config.find('\\'), std::string::npos) << config;
}

TEST(McpClientConfig, ServerPathSitsBesideTheRunningExecutable) {
    const std::filesystem::path server = app::McpServerExecutablePath();
    ASSERT_FALSE(server.empty());
    EXPECT_EQ(server.filename().stem(), "assurance-forge-mcp");
}

// The Preferences toggle and the MCP server's consent gate read and write the
// same typed settings, so a round-trip through them is what guarantees the
// checkbox reflects what the server will actually do.
TEST(McpUserSettingsRoundTrip, TogglingPersistsAndReadsBack) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "af_mcp_client_config_tests";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "settings.json";
    std::filesystem::remove(path);

    EXPECT_FALSE(core::LoadMcpUserSettings(path).enabled) << "absent settings must read as off";

    std::string error;
    ASSERT_TRUE(core::SaveMcpUserSettings(path, core::McpUserSettings{true}, error)) << error;
    EXPECT_TRUE(core::LoadMcpUserSettings(path).enabled);

    ASSERT_TRUE(core::SaveMcpUserSettings(path, core::McpUserSettings{false}, error)) << error;
    EXPECT_FALSE(core::LoadMcpUserSettings(path).enabled);
}
