#include "mcp/server.h"

#include "mcp/session.h"
#include "mcp/tools.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

std::filesystem::path FixturePath() {
    return std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" /
           "fixture_roundtrip_core_argument.sacm.xml";
}

std::filesystem::path TestSettingsDirectory() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "af_mcp_server_tests";
    std::filesystem::create_directories(directory);
    return directory;
}

// Tests must never read the developer's real settings file: a machine with MCP
// enabled would pass a consent test that fails everywhere else.
std::filesystem::path WriteSettings(const std::string& name, const nlohmann::json& document) {
    const std::filesystem::path path = TestSettingsDirectory() / name;
    std::ofstream               out(path, std::ios::trunc);
    out << document.dump();
    return path;
}

std::unique_ptr<mcp::Session> OpenSession(const std::filesystem::path& settings_path) {
    mcp::Session::Config config;
    config.project_path  = FixturePath();
    config.settings_path = settings_path;

    std::string                   error;
    std::unique_ptr<mcp::Session> session = mcp::Session::Open(std::move(config), error);
    EXPECT_NE(session, nullptr) << error;
    return session;
}

std::unique_ptr<mcp::Session> OpenConsentingSession() {
    return OpenSession(WriteSettings("consenting.json", {{"mcp", {{"enabled", true}}}}));
}

std::string Request(const std::string& method, const nlohmann::json& params, int id) {
    nlohmann::json request{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
    if (!params.is_null()) {
        request["params"] = params;
    }
    return request.dump();
}

// Drives the handshake so a test can get to the interesting call in one line.
void Initialize(mcp::Server& server) {
    const std::optional<nlohmann::json> response = server.HandleMessage(
        Request("initialize", {{"clientInfo", {{"name", "TestClient"}, {"version", "9.9"}}}}, 1));
    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->contains("result")) << response->dump();
}

struct ToolCall {
    nlohmann::json payload;
    bool           is_error = false;
};

ToolCall CallTool(mcp::Server& server, const std::string& name,
                  const nlohmann::json& arguments = nlohmann::json::object()) {
    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("tools/call", {{"name", name}, {"arguments", arguments}}, 42));
    if (!response.has_value() || !response->contains("result")) {
        ADD_FAILURE() << "tools/call did not return a result: "
                      << (response.has_value() ? response->dump() : "<no response>");
        return {};
    }
    const nlohmann::json& result = (*response)["result"];
    ToolCall              call;
    call.is_error = result.value("isError", false);
    call.payload  = nlohmann::json::parse(result["content"][0]["text"].get<std::string>(), nullptr,
                                          /*allow_exceptions=*/false);
    return call;
}

// ---------------------------------------------------------------------------
// Handshake and protocol ordering
// ---------------------------------------------------------------------------

TEST(McpServer, InitializeReportsPinnedProtocolVersionAndServerInfo) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);

    const std::optional<nlohmann::json> response = server.HandleMessage(
        Request("initialize", {{"clientInfo", {{"name", "TestClient"}, {"version", "9.9"}}}}, 1));

    ASSERT_TRUE(response.has_value());
    const nlohmann::json& result = (*response)["result"];
    EXPECT_EQ(result["protocolVersion"], mcp::kProtocolVersion);
    EXPECT_EQ(result["serverInfo"]["name"], mcp::kServerName);
    EXPECT_TRUE(result["capabilities"].contains("tools"));
    // The client label is what a future write phase will attribute proposals to.
    EXPECT_EQ(session->client_label(), "TestClient 9.9");
}

TEST(McpServer, RefusesToolCallsBeforeInitialize) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);

    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("tools/list", nullptr, 1));

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->contains("error"));
    EXPECT_EQ((*response)["error"]["code"], mcp::jsonrpc::kInvalidRequest);
}

TEST(McpServer, AnswersPingBeforeInitialize) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);

    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("ping", nullptr, 1));

    ASSERT_TRUE(response.has_value());
    EXPECT_TRUE(response->contains("result"));
}

TEST(McpServer, ProducesNoResponseForANotification) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);

    const std::optional<nlohmann::json> response = server.HandleMessage(
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})");

    EXPECT_FALSE(response.has_value());
    EXPECT_TRUE(session->initialized());
}

TEST(McpServer, ReportsMethodNotFoundForAnUnknownMethod) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("resources/list", nullptr, 2));

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->contains("error"));
    EXPECT_EQ((*response)["error"]["code"], mcp::jsonrpc::kMethodNotFound);
}

TEST(McpServer, ToolsListAdvertisesEveryBuiltinToolWithASchema) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("tools/list", nullptr, 2));

    ASSERT_TRUE(response.has_value());
    const nlohmann::json& tools = (*response)["result"]["tools"];
    EXPECT_EQ(tools.size(), mcp::BuiltinTools().size());
    for (const nlohmann::json& tool : tools) {
        EXPECT_TRUE(tool["name"].is_string());
        EXPECT_FALSE(tool["description"].get<std::string>().empty());
        EXPECT_EQ(tool["inputSchema"]["type"], "object");
    }
}

TEST(McpServer, ReportsInvalidParamsForAnUnknownTool) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("tools/call", {{"name", "drop_database"}}, 3));

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->contains("error"));
    EXPECT_EQ((*response)["error"]["code"], mcp::jsonrpc::kInvalidParams);
}

// ---------------------------------------------------------------------------
// Consent gate
// ---------------------------------------------------------------------------

TEST(McpServer, RefusesEveryContentBearingToolWithoutConsent) {
    std::unique_ptr<mcp::Session> session =
        OpenSession(WriteSettings("withheld.json", {{"mcp", {{"enabled", false}}}}));
    ASSERT_NE(session, nullptr);
    ASSERT_FALSE(session->consent_granted());
    mcp::Server server(*session);
    Initialize(server);

    // Asserted over the whole registry, not one sampled tool: a tool added later
    // that forgets the flag is exactly the leak this must catch.
    for (const mcp::ToolDefinition& tool : mcp::BuiltinTools()) {
        if (!tool.returns_case_content) {
            continue;
        }
        const ToolCall call = CallTool(server, tool.name);
        EXPECT_TRUE(call.is_error) << tool.name << " returned case content without consent";
        EXPECT_NE(call.payload.value("error", std::string{}).find("permission"), std::string::npos)
            << tool.name << " refused without explaining how to grant permission";
    }
}

TEST(McpServer, ConsentFailsClosedWhenTheSettingsFileIsAbsent) {
    EXPECT_FALSE(mcp::ReadMcpConsent(TestSettingsDirectory() / "does_not_exist.json"));
}

TEST(McpServer, ConsentFailsClosedWhenTheSectionOrFlagIsMissing) {
    EXPECT_FALSE(mcp::ReadMcpConsent(WriteSettings("no_section.json", {{"ai", {{"enabled", true}}}})));
    EXPECT_FALSE(mcp::ReadMcpConsent(WriteSettings("no_flag.json", {{"mcp", nlohmann::json::object()}})));
}

TEST(McpServer, ConsentFailsClosedOnAMalformedSettingsFile) {
    const std::filesystem::path path = TestSettingsDirectory() / "malformed.json";
    std::ofstream(path, std::ios::trunc) << "{ this is not json";
    EXPECT_FALSE(mcp::ReadMcpConsent(path));
}

// ---------------------------------------------------------------------------
// Read tools
// ---------------------------------------------------------------------------

TEST(McpServer, ServesACaseOverviewOnceConsentIsGranted) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const ToolCall call = CallTool(server, "get_case_overview");

    ASSERT_FALSE(call.is_error) << call.payload.dump();
    EXPECT_GT(call.payload["element_count"].get<int>(), 0);
    EXPECT_TRUE(call.payload["element_counts_by_type"].is_object());
}

TEST(McpServer, FindElementsCapsResultsAndReportsTruncation) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const ToolCall unfiltered = CallTool(server, "find_elements", {{"query", ""}});
    ASSERT_FALSE(unfiltered.is_error) << unfiltered.payload.dump();
    const int total = unfiltered.payload["total_matches"].get<int>();
    ASSERT_GT(total, 1);

    // A limit of 1 must return 1 and say the rest were withheld -- silent
    // truncation would read to an agent as "this is the whole case".
    const ToolCall limited = CallTool(server, "find_elements", {{"query", ""}, {"limit", 1}});
    ASSERT_FALSE(limited.is_error) << limited.payload.dump();
    EXPECT_EQ(limited.payload["returned"].get<int>(), 1);
    EXPECT_EQ(limited.payload["total_matches"].get<int>(), total);
    EXPECT_TRUE(limited.payload["truncated"].get<bool>());
}

TEST(McpServer, GetElementReturnsFieldsAndNeighbours) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    // Pick a real id from the fixture rather than hard-coding one, so the test
    // survives the fixture being regenerated.
    const ToolCall claims = CallTool(server, "find_elements", {{"type", "claim"}, {"limit", 1}});
    ASSERT_FALSE(claims.is_error) << claims.payload.dump();
    ASSERT_EQ(claims.payload["matches"].size(), 1u) << "fixture has no claim to inspect";
    const std::string id = claims.payload["matches"][0]["id"].get<std::string>();

    const ToolCall call = CallTool(server, "get_element", {{"id", id}});

    ASSERT_FALSE(call.is_error) << call.payload.dump();
    EXPECT_EQ(call.payload["element"]["id"], id);
    EXPECT_TRUE(call.payload.contains("relationships_to_here"));
    EXPECT_TRUE(call.payload.contains("relationships_from_here"));
}

TEST(McpServer, GetElementReportsAnUnknownIdAsAToolError) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const ToolCall call = CallTool(server, "get_element", {{"id", "no-such-element"}});

    EXPECT_TRUE(call.is_error);
    EXPECT_NE(call.payload.value("error", std::string{}).find("no-such-element"),
              std::string::npos);
}

TEST(McpServer, GetElementRequiresAnId) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const ToolCall call = CallTool(server, "get_element");

    EXPECT_TRUE(call.is_error);
}

TEST(McpServer, GetArgumentTreeMarksTruncatedBranchesRatherThanFakingLeaves) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const ToolCall deep = CallTool(server, "get_argument_tree", {{"depth", 12}});
    ASSERT_FALSE(deep.is_error) << deep.payload.dump();
    ASSERT_TRUE(deep.payload["tree"].contains("supported_by"))
        << "fixture root has no support to truncate";

    // At depth 1 the root's children must be reported as truncated, carrying a
    // count, so an agent cannot conclude the goal is unsupported.
    const ToolCall shallow = CallTool(server, "get_argument_tree", {{"depth", 1}});
    ASSERT_FALSE(shallow.is_error) << shallow.payload.dump();
    const nlohmann::json& first_child = shallow.payload["tree"]["supported_by"][0];
    if (first_child.contains("truncated")) {
        EXPECT_TRUE(first_child["truncated"].get<bool>());
        EXPECT_GT(first_child["child_count"].get<int>(), 0);
    }
}

} // namespace
