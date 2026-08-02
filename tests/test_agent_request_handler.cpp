#include "app/agent_request_handler.h"

#include "core/app_state.h"
#include "parser/model_utils.h"

#include <gtest/gtest.h>

#include <cstdint>
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

TEST(AgentRequestHandler, ConnectedReadsUseAndIdentifyTheIntegratedWorkingDraft) {
    TempDir workspace{UniqueTempPath("working-draft")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    parser::AssuranceCase working = state.loaded_case.value();
    parser::SacmElement proposed;
    proposed.id = "MCP-DRAFT-GOAL";
    proposed.type = "claim";
    proposed.content = "Visible only in the integrated working draft";
    working.elements.push_back(proposed);

    core::drafts::DraftWorkspace draft;
    draft.id = "draft-test";
    draft.argument_file = "arguments/main.sacm";
    draft.working_revision = 12;

    app::AgentRequestContext context{state, workspace.path.string(), "test client", {}};
    context.current_argument_view = [&working, &draft] { return app::AgentArgumentView{&working, &draft, true}; };

    const bridge::Response response =
        app::HandleAgentRequest(MakeRequest("get_element", {{"id", proposed.id}}), context);

    ASSERT_TRUE(response.ok) << response.error_message;
    ASSERT_FALSE(response.result.value("isError", true)) << response.result.dump();
    EXPECT_EQ(response.result["element"]["content"], proposed.content);
    EXPECT_EQ(response.result["argument_file"], "arguments/main.sacm");
    EXPECT_EQ(response.result["view"], "working_draft");
    EXPECT_EQ(response.result["workspace_id"], draft.id);
    EXPECT_EQ(response.result["working_revision"], draft.working_revision);
}

TEST(AgentRequestHandler, ConnectedAcceptedReadReportsTheRevisionNeededToStartTheFirstGroup) {
    TempDir workspace{UniqueTempPath("connected-accepted")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    app::AgentRequestContext context{state, workspace.path.string(), "test client", {}};
    context.current_argument_view = [&state] {
        return app::AgentArgumentView{&state.loaded_case.value(), nullptr, false};
    };

    const bridge::Response response = app::HandleAgentRequest(MakeRequest("get_case_overview"), context);

    ASSERT_FALSE(response.result.value("isError", true)) << response.result.dump();
    EXPECT_EQ(response.result["view"], "accepted");
    EXPECT_EQ(response.result["working_revision"], 0);
}

TEST(AgentRequestHandler, McpDraftGroupsAreRevisionCheckedAndVisibleToSubsequentReads) {
    TempDir workspace{UniqueTempPath("draft-group")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 41;
    context.source_session_id = "stable-mcp-session-41";
    context.current_argument_view = [&] {
        const core::drafts::DraftWorkspace* draft = drafts.workspace();
        if (draft == nullptr || !draft->has_active_groups())
            return app::AgentArgumentView{&state.loaded_case.value(), nullptr};
        const core::drafts::DraftMaterializationResult& materialized =
            drafts.Materialize(state.loaded_case.value(), state.case_revision);
        return app::AgentArgumentView{&materialized.working_model, draft, true};
    };

    const bridge::Response begun =
        app::HandleAgentRequest(MakeRequest("begin_change_group",
                                            {{"title", "Add a monitored hazard"},
                                             {"rationale", "The existing argument does not address monitoring."},
                                             {"expected_working_revision", 0}}),
                                context);
    ASSERT_TRUE(begun.ok);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
    const std::string group_id = begun.result["group_id"].get<std::string>();
    const std::uint64_t begun_revision = begun.result["working_revision"].get<std::uint64_t>();

    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", group_id},
                     {"expected_working_revision", begun_revision},
                     {"operations",
                      nlohmann::json::array({{{"type", "CreateClaim"},
                                              {"create_ref", "$monitoring"},
                                              {"text", "Monitoring detects unsafe blender operation"}}})}}),
        context);
    ASSERT_TRUE(staged.ok);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();
    const std::string created_id = staged.result["created_element_ids"]["$monitoring"].get<std::string>();
    const std::uint64_t staged_revision = staged.result["working_revision"].get<std::uint64_t>();
    EXPECT_GT(staged_revision, begun_revision);

    const bridge::Response developed = app::HandleAgentRequest(
        MakeRequest(
            "stage_operations",
            {{"group_id", group_id},
             {"expected_working_revision", staged_revision},
             {"operations",
              nlohmann::json::array({{{"type", "UpdateElementText"},
                                      {"element", {{"id", created_id}}},
                                      {"field", "content"},
                                      {"old_value", "Monitoring detects unsafe blender operation"},
                                      {"new_value", "Monitoring detects and reports unsafe blender operation"}}})}}),
        context);
    ASSERT_FALSE(developed.result.value("isError", true)) << developed.result.dump();
    const std::uint64_t developed_revision = developed.result["working_revision"].get<std::uint64_t>();
    ASSERT_TRUE(developed.result.contains("operations"));
    ASSERT_EQ(developed.result["operations"].size(), 2u);
    EXPECT_EQ(developed.result["operations"][0]["type"], "CreateClaim");
    EXPECT_EQ(developed.result["operations"][1]["element"]["id"], created_id);

    const bridge::Response read = app::HandleAgentRequest(MakeRequest("get_element", {{"id", created_id}}), context);
    ASSERT_FALSE(read.result.value("isError", true)) << read.result.dump();
    EXPECT_EQ(read.result["element"]["content"], "Monitoring detects and reports unsafe blender operation");
    EXPECT_EQ(read.result["view"], "working_draft");

    const bridge::Response stale = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", group_id},
                     {"expected_working_revision", staged_revision},
                     {"operations",
                      nlohmann::json::array({{{"type", "UpdateElementText"},
                                              {"element", {{"id", created_id}}},
                                              {"field", "content"},
                                              {"old_value", "Monitoring detects unsafe blender operation"},
                                              {"new_value", "This stale edit must be refused"}}})}}),
        context);
    EXPECT_TRUE(stale.result.value("isError", false));
    EXPECT_EQ(stale.result["current_working_revision"], developed_revision);
    EXPECT_EQ(drafts.revision(), developed_revision);

    const bridge::Response events =
        app::HandleAgentRequest(MakeRequest("get_draft_events", {{"after_revision", begun_revision}}), context);
    ASSERT_FALSE(events.result.value("isError", true));
    EXPECT_GE(events.result["events"].size(), 2u);

    const bridge::Response submitted = app::HandleAgentRequest(
        MakeRequest("submit_change_group", {{"group_id", group_id}, {"expected_working_revision", developed_revision}}),
        context);
    ASSERT_FALSE(submitted.result.value("isError", true)) << submitted.result.dump();
    EXPECT_EQ(submitted.result["state"], "ready");

    EXPECT_EQ(parser::FindElementById(state.loaded_case.value(), created_id), nullptr)
        << "staging through MCP must not mutate the accepted model";
    core::drafts::DraftWorkspaceStore reopened;
    reopened.SetProjectRoot(workspace.path);
    ASSERT_TRUE(reopened.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;
    const core::drafts::DraftChangeGroup* restored = reopened.workspace()->FindGroup(group_id);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->source, core::drafts::DraftSource::Mcp);
    EXPECT_EQ(restored->source_label, "MCP test client");
    EXPECT_EQ(restored->source_session_id, "stable-mcp-session-41");
    EXPECT_EQ(restored->generated_ids.at("$monitoring"), created_id);
    EXPECT_EQ(restored->state, core::drafts::DraftGroupState::Ready);
}

TEST(AgentRequestHandler, HumanDraftMutationMakesAnMcpRevisionStale) {
    TempDir workspace{UniqueTempPath("cross-source-stale")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 7;
    const bridge::Response begun = app::HandleAgentRequest(
        MakeRequest("begin_change_set", {{"title", "Legacy alias"}, {"expected_working_revision", 0}}), context);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
    EXPECT_EQ(begun.result["group_id"], begun.result["change_set_id"]);
    const std::uint64_t mcp_revision = begun.result["working_revision"].get<std::uint64_t>();

    core::drafts::DraftGroupRequest human;
    human.title = "Human clarification";
    human.source = core::drafts::DraftSource::Human;
    human.source_label = "Reviewer";
    ASSERT_FALSE(drafts.BeginGroup(human, state.loaded_case.value(), error).empty()) << error;

    const bridge::Response stale = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", begun.result["group_id"]},
                     {"expected_working_revision", mcp_revision},
                     {"operations",
                      nlohmann::json::array({{{"type", "CreateClaim"},
                                              {"create_ref", "$stale"},
                                              {"text", "Must not be staged against an unseen human edit"}}})}}),
        context);
    EXPECT_TRUE(stale.result.value("isError", false));
    EXPECT_EQ(stale.result["current_working_revision"], drafts.revision());
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
