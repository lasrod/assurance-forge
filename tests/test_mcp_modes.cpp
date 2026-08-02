#include "mcp/server.h"

#include "mcp/session.h"
#include "mcp/tools.h"

#include "core/app_state.h"
#include "core/project_service.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>

// A session runs in one of two modes, and these pin what each one is allowed to
// do.
//
// Offline -- no Assurance Forge running -- answers reads from a copy it loaded
// and refuses everything else. That is not a limitation to work around: a
// headless process has no command bus, no audit log, and nobody to accept a
// change, and a second writer in the project directory is the fault this whole
// design removes.

namespace {

struct TempDir {
    std::filesystem::path path;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path UniqueTempPath(const std::string& stem) {
    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_mcp_modes_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path WriteConsentingSettings(const std::filesystem::path& directory) {
    const std::filesystem::path path = directory / "settings.json";
    std::ofstream(path, std::ios::trunc) << R"({"mcp":{"enabled":true}})";
    return path;
}

struct Fixture {
    TempDir workspace;
    std::filesystem::path project_root;
    std::unique_ptr<mcp::Session> session;
};

// Builds a real project on disk (the seed document carries one claim), then
// opens an offline MCP session against it. `never_connect` keeps the test
// offline even when the developer running it happens to have the same project
// open in Assurance Forge.
std::unique_ptr<Fixture> MakeOfflineFixture(const std::string& stem) {
    std::unique_ptr<Fixture> fixture(new Fixture{TempDir{UniqueTempPath(stem)}, {}, {}});

    core::AppState builder;
    if (!builder.create_empty_project("Project", fixture->workspace.path.string())) {
        ADD_FAILURE() << "could not create project: " << builder.status_message;
        return nullptr;
    }
    fixture->project_root = builder.current_project->rootPath;

    mcp::Session::Config config;
    config.project_path = fixture->project_root;
    config.settings_path = WriteConsentingSettings(fixture->workspace.path);
    config.never_connect = true;

    std::string error;
    fixture->session = mcp::Session::Open(std::move(config), error);
    if (fixture->session == nullptr) {
        ADD_FAILURE() << "could not open MCP session: " << error;
        return nullptr;
    }
    return fixture;
}

std::string Request(const std::string& method, const nlohmann::json& params, int id) {
    nlohmann::json request{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
    if (!params.is_null()) {
        request["params"] = params;
    }
    return request.dump();
}

void Initialize(mcp::Server& server) {
    const std::optional<nlohmann::json> response =
        server.HandleMessage(Request("initialize", {{"clientInfo", {{"name", "TestClient"}, {"version", "2.0"}}}}, 1));
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
        server.HandleMessage(Request("tools/call", {{"name", name}, {"arguments", arguments}}, 9));
    if (!response.has_value() || !response->contains("result")) {
        ADD_FAILURE() << "tools/call returned no result: " << (response.has_value() ? response->dump() : "<none>");
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

// Every file under `root`, with its size and content hash, so a test can assert
// that a whole directory tree is untouched rather than checking the two or three
// files it happened to think of.
std::map<std::string, std::string> SnapshotTree(const std::filesystem::path& root) {
    std::map<std::string, std::string> snapshot;
    std::error_code ec;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream file(entry.path(), std::ios::binary);
        std::ostringstream contents;
        contents << file.rdbuf();
        snapshot[std::filesystem::relative(entry.path(), root, ec).generic_string()] = contents.str();
    }
    return snapshot;
}

} // namespace

TEST(McpOffline, ReadsTheCaseWithNoApplicationRunning) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("read");
    ASSERT_NE(fixture, nullptr);
    EXPECT_EQ(fixture->session->mode(), mcp::Session::Mode::Offline);

    mcp::Server server(*fixture->session);
    Initialize(server);

    const ToolCall overview = CallTool(server, "get_case_overview");
    ASSERT_FALSE(overview.is_error) << overview.payload.dump();
    EXPECT_GT(overview.payload["element_count"].get<int>(), 0);
    EXPECT_EQ(overview.payload["view"], "accepted");
    EXPECT_FALSE(overview.payload.contains("workspace_id"));
    EXPECT_FALSE(overview.payload.contains("working_revision"));
}

TEST(McpOffline, SearchesAndFetchesElements) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("search");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);

    const ToolCall claims = CallTool(server, "find_elements", {{"type", "claim"}, {"limit", 1}});
    ASSERT_FALSE(claims.is_error) << claims.payload.dump();
    ASSERT_FALSE(claims.payload["matches"].empty());

    const std::string id = claims.payload["matches"][0]["id"].get<std::string>();
    const ToolCall element = CallTool(server, "get_element", {{"id", id}});
    ASSERT_FALSE(element.is_error) << element.payload.dump();
    EXPECT_EQ(element.payload["element"]["id"], id);
}

// The point of the mode. Anything that changes a safety case needs machinery a
// headless process does not have, and the refusal has to say so in terms the
// user can act on -- "open the project in Assurance Forge" -- rather than
// reporting a generic failure the model will simply retry.
TEST(McpOffline, RefusesToChangeAnythingAndSaysWhy) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("readonly");
    ASSERT_NE(fixture, nullptr);

    const mcp::Session::OperationResult result = fixture->session->Run("stage_operations", nlohmann::json::object());

    EXPECT_TRUE(result.is_error);
    EXPECT_TRUE(result.needs_application);
    const std::string message = result.payload["error"].get<std::string>();
    EXPECT_NE(message.find("Assurance Forge"), std::string::npos) << message;
}

// The strongest statement of the single-writer rule: run every tool the server
// publishes and assert the project directory is byte-for-byte what it was.
// Earlier designs wrote a proposal file, a manifest entry and a review item from
// this process, and the application reverted two of the three on its next save.
TEST(McpOffline, LeavesEveryProjectFileByteIdentical) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("nowrites");
    ASSERT_NE(fixture, nullptr);

    const std::map<std::string, std::string> before = SnapshotTree(fixture->project_root);
    ASSERT_FALSE(before.empty()) << "fixture wrote no project files, so this proves nothing";

    mcp::Server server(*fixture->session);
    Initialize(server);
    for (const mcp::ToolDefinition& tool : mcp::BuiltinTools()) {
        CallTool(server, tool.name, {{"path", "does-not-exist.sacm"}, {"id", "G1"}});
    }

    EXPECT_EQ(SnapshotTree(fixture->project_root), before);
}

TEST(McpOffline, ListsEveryArgumentAndMarksTheLoadedOne) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("listfiles");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);

    const ToolCall listed = CallTool(server, "list_case_files");
    ASSERT_FALSE(listed.is_error) << listed.payload.dump();
    ASSERT_FALSE(listed.payload["case_files"].empty());

    int loaded = 0;
    for (const nlohmann::json& entry : listed.payload["case_files"]) {
        loaded += entry.value("loaded", false) ? 1 : 0;
    }
    EXPECT_EQ(loaded, 1) << listed.payload.dump();
}

TEST(McpOffline, ReportsAnUnknownPathRatherThanSilentlyKeepingTheOldCase) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("badpath");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);

    const ToolCall switched = CallTool(server, "open_case_file", {{"path", "nope.sacm"}});
    EXPECT_TRUE(switched.is_error);
    EXPECT_NE(switched.payload["error"].get<std::string>().find("nope.sacm"), std::string::npos);
}

// Consent is checked per call and fails closed, so a session opened while the
// switch was on stops serving the moment it goes off.
TEST(McpOffline, StopsServingWhenConsentIsWithdrawn) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("consent");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);

    ASSERT_FALSE(CallTool(server, "get_case_overview").is_error);

    std::ofstream(fixture->workspace.path / "settings.json", std::ios::trunc) << R"({"mcp":{"enabled":false}})";

    const ToolCall refused = CallTool(server, "get_case_overview");
    EXPECT_TRUE(refused.is_error);
    EXPECT_NE(refused.payload["error"].get<std::string>().find("Preferences"), std::string::npos);
}

// "Add argumentation about Z where it fits best" is not answerable from
// substring search: `find_elements` says where a word appears, which is a
// different question from where an argument belongs. An agent left to guess
// attaches things at the root.
TEST(McpOffline, SuggestsWhereNewArgumentWouldFit) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("placement");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);

    const ToolCall overview = CallTool(server, "get_case_overview");
    ASSERT_FALSE(overview.is_error) << overview.payload.dump();
    const std::string case_name = overview.payload.value("case_name", std::string("safety"));

    const ToolCall placed = CallTool(server, "suggest_placement", {{"topic", case_name + " thermal hazards"}});
    ASSERT_FALSE(placed.is_error) << placed.payload.dump();
    ASSERT_TRUE(placed.payload.contains("suggestions"));

    for (const nlohmann::json& suggestion : placed.payload["suggestions"]) {
        // Only goals and strategies host new argument. Offering a solution as an
        // anchor would invite exactly the structural mistake the SCCG checks
        // then report.
        const std::string role = suggestion["role"].get<std::string>();
        EXPECT_TRUE(role == "Goal" || role == "Strategy") << role;
        // The path is the single most useful thing here: it says what the branch
        // is about without needing the whole tree.
        EXPECT_TRUE(suggestion.contains("path_from_root"));
        EXPECT_TRUE(suggestion.contains("existing_sub_claims"));
    }
}

TEST(McpOffline, RequiresATopicToSuggestPlacement) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("notopic");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);

    const ToolCall placed = CallTool(server, "suggest_placement");
    EXPECT_TRUE(placed.is_error);
}

// Switching between a project's arguments had no test that actually switched:
// the seeded fixture holds one argument, so every existing case exercised the
// refusal path and none exercised success. A second argument is added here so
// the tool is covered doing the thing it exists for.
TEST(McpOffline, SwitchesToASecondArgumentAndKeepsReadingIt) {
    std::unique_ptr<Fixture> fixture = MakeOfflineFixture("switch");
    ASSERT_NE(fixture, nullptr);

    // A second, deliberately distinguishable argument in the same project.
    core::AppState builder;
    ASSERT_TRUE(builder.open_project(fixture->project_root.string())) << builder.status_message;
    core::ProjectFileEntry added;
    std::string error;
    ASSERT_TRUE(core::ProjectService::AddSacmFile(builder.current_project.value(), "second", added, error)) << error;
    ASSERT_TRUE(core::ProjectService::WriteManifestSafely(builder.current_project.value(), error)) << error;

    // Reopen so the session sees the manifest the project now has.
    mcp::Session::Config config;
    config.project_path = fixture->project_root;
    config.settings_path = WriteConsentingSettings(fixture->workspace.path);
    config.never_connect = true;
    std::unique_ptr<mcp::Session> session = mcp::Session::Open(std::move(config), error);
    ASSERT_NE(session, nullptr) << error;

    mcp::Server server(*session);
    Initialize(server);

    const ToolCall listed = CallTool(server, "list_case_files");
    ASSERT_FALSE(listed.is_error) << listed.payload.dump();
    ASSERT_EQ(listed.payload["case_files"].size(), 2u) << listed.payload.dump();

    std::string other;
    for (const nlohmann::json& entry : listed.payload["case_files"]) {
        if (!entry.value("loaded", false)) {
            other = entry["path"].get<std::string>();
        }
    }
    ASSERT_FALSE(other.empty()) << listed.payload.dump();

    const ToolCall switched = CallTool(server, "open_case_file", {{"path", other}});
    ASSERT_FALSE(switched.is_error) << switched.payload.dump();
    // The overview must describe the file it moved TO. Returning the previous
    // one would have an agent reason about a document it just navigated away
    // from, which is indistinguishable from the switch not happening.
    EXPECT_NE(switched.payload["loaded_file"].get<std::string>().find(other), std::string::npos)
        << switched.payload.dump();

    // And the switch sticks for subsequent calls.
    const ToolCall after = CallTool(server, "list_case_files");
    ASSERT_FALSE(after.is_error) << after.payload.dump();
    for (const nlohmann::json& entry : after.payload["case_files"]) {
        if (entry["path"].get<std::string>() == other) {
            EXPECT_TRUE(entry.value("loaded", false)) << after.payload.dump();
        }
    }
}
