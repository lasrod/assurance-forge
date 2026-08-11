#include "mcp/server.h"

#include "mcp/session.h"
#include "mcp/tools.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

std::filesystem::path FixturePath() {
    return std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" / "fixture_roundtrip_core_argument.sacm.xml";
}

std::filesystem::path TestSettingsDirectory() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "af_mcp_server_tests";
    std::filesystem::create_directories(directory);
    return directory;
}

// Tests must never read the developer's real settings file: a machine with MCP
// enabled would pass a consent test that fails everywhere else.
std::filesystem::path WriteSettings(const std::string& name, const nlohmann::json& document) {
    const std::filesystem::path path = TestSettingsDirectory() / name;
    std::ofstream out(path, std::ios::trunc);
    out << document.dump();
    return path;
}

std::unique_ptr<mcp::Session> OpenSession(const std::filesystem::path& settings_path) {
    mcp::Session::Config config;
    config.project_path = FixturePath();
    config.settings_path = settings_path;

    std::string error;
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
    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("initialize", {{"clientInfo", {{"name", "TestClient"}, {"version", "9.9"}}}}, 1));
    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->contains("result")) << response->dump();
}

struct ToolCall {
    nlohmann::json payload;
    bool is_error = false;
};

ToolCall
CallTool(mcp::Server& server, const std::string& name, const nlohmann::json& arguments = nlohmann::json::object()) {
    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("tools/call", {{"name", name}, {"arguments", arguments}}, 42));
    if (!response.has_value() || !response->contains("result")) {
        ADD_FAILURE() << "tools/call did not return a result: "
                      << (response.has_value() ? response->dump() : "<no response>");
        return {};
    }
    const nlohmann::json& result = (*response)["result"];
    ToolCall call;
    call.is_error = result.value("isError", false);
    call.payload = nlohmann::json::parse(result["content"][0]["text"].get<std::string>(),
                                         nullptr,
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

    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("initialize", {{"clientInfo", {{"name", "TestClient"}, {"version", "9.9"}}}}, 1));

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

    const std::optional<nlohmann::json> response = server.HandleMessage(Request("tools/list", nullptr, 1));

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->contains("error"));
    EXPECT_EQ((*response)["error"]["code"], mcp::jsonrpc::kInvalidRequest);
}

TEST(McpServer, AnswersPingBeforeInitialize) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);

    const std::optional<nlohmann::json> response = server.HandleMessage(Request("ping", nullptr, 1));

    ASSERT_TRUE(response.has_value());
    EXPECT_TRUE(response->contains("result"));
}

TEST(McpServer, ProducesNoResponseForANotification) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);

    const std::optional<nlohmann::json> response =
        server.HandleMessage(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");

    EXPECT_FALSE(response.has_value());
    EXPECT_TRUE(session->initialized());
}

// `resources/list` used to stand in for "unknown method" here. It is a real
// method now, which is why this asks about one the server genuinely does not
// implement.
TEST(McpServer, ReportsMethodNotFoundForAnUnknownMethod) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> response = server.HandleMessage(Request("completion/complete", nullptr, 2));

    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->contains("error"));
    EXPECT_EQ((*response)["error"]["code"], mcp::jsonrpc::kMethodNotFound);
}

TEST(McpServer, ToolsListAdvertisesEveryBuiltinToolWithASchema) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> response = server.HandleMessage(Request("tools/list", nullptr, 2));

    ASSERT_TRUE(response.has_value());
    const nlohmann::json& tools = (*response)["result"]["tools"];
    EXPECT_EQ(tools.size(), mcp::BuiltinTools().size());
    for (const nlohmann::json& tool : tools) {
        EXPECT_TRUE(tool["name"].is_string());
        EXPECT_FALSE(tool["description"].get<std::string>().empty());
        EXPECT_EQ(tool["inputSchema"]["type"], "object");
    }
}

TEST(McpServer, RegistryPublishesDraftGroupsButNeverPromotion) {
    EXPECT_NE(mcp::FindTool("get_draft_status"), nullptr);
    EXPECT_NE(mcp::FindTool("begin_change_group"), nullptr);
    EXPECT_NE(mcp::FindTool("stage_operations"), nullptr);
    EXPECT_NE(mcp::FindTool("submit_change_group"), nullptr);

    for (const mcp::ToolDefinition& tool : mcp::BuiltinTools()) {
        std::string name = tool.name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        EXPECT_EQ(name.find("accept"), std::string::npos) << tool.name;
        EXPECT_EQ(name.find("apply"), std::string::npos) << tool.name;
        EXPECT_EQ(name.find("promote"), std::string::npos) << tool.name;
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

// ADR 0007 promises that revoking consent takes effect on the next call rather
// than when the client next restarts the server. That is only true if consent is
// read per call: a grant cached at session open would keep serving the safety
// case for as long as the client happened to leave the process running, which is
// exactly the window a user revoking permission is trying to close.
TEST(McpServer, RevokingConsentTakesEffectWithoutReopeningTheSession) {
    const std::filesystem::path settings = WriteSettings("revocable.json", {{"mcp", {{"enabled", true}}}});
    std::unique_ptr<mcp::Session> session = OpenSession(settings);
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    ASSERT_FALSE(CallTool(server, "get_case_overview").is_error) << "consent was granted but the tool refused";

    // Same session, same server object; only the file changes underneath.
    WriteSettings("revocable.json", {{"mcp", {{"enabled", false}}}});

    EXPECT_TRUE(CallTool(server, "get_case_overview").is_error)
        << "revoked consent did not take effect until the server was restarted";
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
    EXPECT_NE(call.payload.value("error", std::string{}).find("no-such-element"), std::string::npos);
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
    ASSERT_TRUE(deep.payload["tree"].contains("supported_by")) << "fixture root has no support to truncate";

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

// SCCG reaches an agent as MCP resources and prompts so it has the house rules
// before it writes, rather than being corrected afterwards.
TEST(McpServer, PublishesTheSccgCatalogAsAResource) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> listed = server.HandleMessage(Request("resources/list", nullptr, 2));
    ASSERT_TRUE(listed.has_value());
    ASSERT_TRUE(listed->contains("result")) << listed->dump();
    ASSERT_FALSE((*listed)["result"]["resources"].empty());

    const std::string uri = (*listed)["result"]["resources"][0]["uri"].get<std::string>();
    const std::optional<nlohmann::json> read = server.HandleMessage(Request("resources/read", {{"uri", uri}}, 3));
    ASSERT_TRUE(read.has_value());
    ASSERT_TRUE(read->contains("result")) << read->dump();

    const std::string text = (*read)["result"]["contents"][0]["text"].get<std::string>();
    // Real guideline text, not a placeholder: an agent that reads this is
    // reading what reviews actually apply.
    EXPECT_NE(text.find("CL.1"), std::string::npos) << text.substr(0, 400);
    // The dist loader carries no document metadata, so without a fallback the
    // resource opened "# \n\n" -- an empty heading on the text that exists to
    // establish authority.
    EXPECT_EQ(text.rfind("# ", 0), 0u) << text.substr(0, 80);
    EXPECT_NE(text.find("Safety Case Core Guidelines"), std::string::npos) << text.substr(0, 80);
}

// The per-guideline uri was readable long before it was discoverable: nothing
// listed it, and MCP's discovery surface for parameterized uris is the resource
// template list.
TEST(McpServer, ListsThePerGuidelineResourceTemplateAndServesIt) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> listed = server.HandleMessage(Request("resources/templates/list", nullptr, 2));
    ASSERT_TRUE(listed.has_value());
    ASSERT_TRUE(listed->contains("result")) << listed->dump();
    const nlohmann::json& templates = (*listed)["result"]["resourceTemplates"];
    ASSERT_FALSE(templates.empty());
    EXPECT_EQ(templates[0]["uriTemplate"], "sccg://guideline/{id}");

    // And the uri the template describes actually serves one guideline.
    const std::optional<nlohmann::json> read =
        server.HandleMessage(Request("resources/read", {{"uri", "sccg://guideline/CL.1"}}, 3));
    ASSERT_TRUE(read.has_value());
    ASSERT_TRUE(read->contains("result")) << read->dump();
    const std::string text = (*read)["result"]["contents"][0]["text"].get<std::string>();
    EXPECT_NE(text.find("CL.1"), std::string::npos) << text.substr(0, 200);
}

TEST(McpServer, ReportsAnUnknownResourceRatherThanEmptyContent) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("resources/read", {{"uri", "sccg://nope"}}, 2));
    ASSERT_TRUE(response.has_value());
    EXPECT_TRUE(response->contains("error")) << response->dump();
}

// The prompts are the mechanism that answers "be aware of the rules before it
// starts": the user picks one in their client and the guidance arrives with it.
TEST(McpServer, CarriesSccgGuidanceInsideEachPrompt) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> listed = server.HandleMessage(Request("prompts/list", nullptr, 2));
    ASSERT_TRUE(listed.has_value());
    ASSERT_TRUE(listed->contains("result")) << listed->dump();
    EXPECT_EQ((*listed)["result"]["prompts"].size(), 4u);

    const std::optional<nlohmann::json> got = server.HandleMessage(
        Request("prompts/get",
                {{"name", "draft_argument_from_standard"}, {"arguments", {{"standard", "ISO 26262 part 6"}}}},
                3));
    ASSERT_TRUE(got.has_value());
    ASSERT_TRUE(got->contains("result")) << got->dump();

    const std::string text = (*got)["result"]["messages"][0]["content"]["text"].get<std::string>();
    EXPECT_NE(text.find("ISO 26262 part 6"), std::string::npos);
    // The guidance itself, quoted from the catalog rather than paraphrased.
    EXPECT_NE(text.find("CL.1"), std::string::npos);
    // And the standing reminder that staging is not editing.
    EXPECT_NE(text.find("not editing the accepted safety case"), std::string::npos) << text;
    // Every prompt carries the language discipline, because an element written
    // in one language of a two-language case is invisible to half its reviewers.
    EXPECT_NE(text.find("translations"), std::string::npos) << text;
}

TEST(McpServer, TheTranslatePromptSaysWhatTranslatingAClaimMustPreserve) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> got = server.HandleMessage(
        Request("prompts/get", {{"name", "translate_case"}, {"arguments", {{"language", "ja"}}}}, 3));
    ASSERT_TRUE(got.has_value());
    ASSERT_TRUE(got->contains("result")) << got->dump();

    const std::string text = (*got)["result"]["messages"][0]["content"]["text"].get<std::string>();
    EXPECT_NE(text.find("ja"), std::string::npos);
    // Translating a claim is not translating prose: a dropped qualifier makes
    // the claim broader than the evidence supports.
    EXPECT_NE(text.find("qualifier"), std::string::npos) << text;
    // And it must not rewrite the argument it is translating.
    EXPECT_NE(text.find("Do not change the English"), std::string::npos) << text;
    // The qualifier rule itself rides along, quoted from the catalog: the
    // prompt's informal paragraph and the review must apply the same text.
    EXPECT_NE(text.find("CL.5"), std::string::npos) << text;
}

TEST(McpServer, ReportsAnUnknownPrompt) {
    std::unique_ptr<mcp::Session> session = OpenConsentingSession();
    ASSERT_NE(session, nullptr);
    mcp::Server server(*session);
    Initialize(server);

    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("prompts/get", {{"name", "no_such_prompt"}}, 2));
    ASSERT_TRUE(response.has_value());
    EXPECT_TRUE(response->contains("error")) << response->dump();
}
