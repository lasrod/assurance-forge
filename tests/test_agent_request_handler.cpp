#include "app/agent_request_handler.h"

#include "core/app_state.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

// What a connected AI client gets back when the application answers.
//
// The handler is a free function over the state it needs precisely so this can
// run without a window, an event loop or a controller graph -- the previous
// design's decision logic was only ever exercised through a running
// application, and its own pull request records that it never fired there.

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
        std::filesystem::temp_directory_path() / ("af_agent_handler_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

// Creating a project writes its files but loads none of them, so a state built
// that way has no case at all. The application opens an argument on project
// open; this reproduces that, or the tests would only ever exercise the
// no-case path.
bool OpenProjectWithArgument(core::AppState& state, const std::filesystem::path& workspace) {
    if (!state.create_empty_project("Project", workspace.string())) {
        ADD_FAILURE() << "could not create project: " << state.status_message;
        return false;
    }
    for (const core::ProjectFileEntry& entry : state.current_project->files) {
        if (entry.role == core::ProjectFileRole::SacmArgument) {
            return state.open_project_file(entry);
        }
    }
    ADD_FAILURE() << "seeded project holds no SACM argument";
    return false;
}

bridge::Request MakeRequest(const std::string& op, const nlohmann::json& args = {}) {
    bridge::Request request;
    request.id = 7;
    request.op = op;
    request.args = args.is_object() ? args : nlohmann::json::object();
    return request;
}

} // namespace

TEST(AgentRequestHandler, AnswersAnOverviewFromTheLoadedModel) {
    TempDir workspace{UniqueTempPath("overview")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    const app::AgentRequestContext context{state, workspace.path.string(), "test client", {}};
    const bridge::Response response = app::HandleAgentRequest(MakeRequest("get_case_overview"), context);

    ASSERT_TRUE(response.ok) << response.error_message;
    EXPECT_FALSE(response.result.value("isError", true));
    EXPECT_GT(response.result["element_count"].get<int>(), 0);
}

// A domain failure is a successful response carrying `isError`, not a transport
// failure. The distinction matters: a model that sees a faulted connection stops,
// where one that sees a tool error corrects itself and tries again.
TEST(AgentRequestHandler, ReportsAnUnknownElementAsAToolErrorNotATransportError) {
    TempDir workspace{UniqueTempPath("unknown")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    const app::AgentRequestContext context{state, workspace.path.string(), "test client", {}};
    const bridge::Response response = app::HandleAgentRequest(MakeRequest("get_element", {{"id", "NOPE"}}), context);

    EXPECT_TRUE(response.ok);
    EXPECT_TRUE(response.result.value("isError", false));
    EXPECT_NE(response.result["error"].get<std::string>().find("NOPE"), std::string::npos);
}

// An unknown operation is a genuine protocol failure, and its message has to
// point at the likely cause: two binaries from different builds.
TEST(AgentRequestHandler, RefusesAnUnknownOperationWithAVersionHint) {
    TempDir workspace{UniqueTempPath("unknownop")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    const app::AgentRequestContext context{state, workspace.path.string(), "test client", {}};
    const bridge::Response response = app::HandleAgentRequest(MakeRequest("do_something_new"), context);

    EXPECT_FALSE(response.ok);
    EXPECT_EQ(response.error_code, bridge::error_code::kUnknownOperation);
    EXPECT_NE(response.error_message.find("assurance-forge-mcp"), std::string::npos);
}

// Switching argument files is the runtime's job, not the handler's, so the
// handler must refuse rather than pretend when the caller supplied no way to do
// it.
TEST(AgentRequestHandler, RefusesToSwitchFilesWithoutACallback) {
    TempDir workspace{UniqueTempPath("noswitch")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    const app::AgentRequestContext context{state, workspace.path.string(), "test client", {}};
    const bridge::Response response =
        app::HandleAgentRequest(MakeRequest("open_case_file", {{"path", "arguments/main.sacm"}}), context);

    EXPECT_TRUE(response.ok);
    EXPECT_TRUE(response.result.value("isError", false));
}

TEST(AgentRequestHandler, PassesTheRequestedPathToTheRuntime) {
    TempDir workspace{UniqueTempPath("switch")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    std::string asked_for;
    app::AgentRequestContext context{
        state, workspace.path.string(), "test client", [&asked_for](const std::string& path, std::string&) {
            asked_for = path;
            return true;
        }};
    const bridge::Response response =
        app::HandleAgentRequest(MakeRequest("open_case_file", {{"path", "arguments/main2.sacm"}}), context);

    EXPECT_EQ(asked_for, "arguments/main2.sacm");
    EXPECT_TRUE(response.ok);
}

TEST(AgentRequestHandler, RequiresAPathToSwitchFiles) {
    TempDir workspace{UniqueTempPath("nopath")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    bool called = false;
    app::AgentRequestContext context{
        state, workspace.path.string(), "test client", [&called](const std::string&, std::string&) {
            called = true;
            return true;
        }};
    const bridge::Response response = app::HandleAgentRequest(MakeRequest("open_case_file"), context);

    EXPECT_FALSE(called);
    EXPECT_TRUE(response.result.value("isError", false));
}
