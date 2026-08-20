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

// ADR 0014: the working revision says the draft moved; the context generation
// says the ground under the whole session moved -- a project switch, a fresh
// grant, a revocation. A mutation computed before such a change must not land
// after it, and every read names the generation so a client can comply.
TEST(AgentRequestHandler, MutationsNameTheContextGenerationTheyRead) {
    TempDir workspace{UniqueTempPath("context-generation")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 43;
    context.source_session_id = "stable-mcp-session-43";
    context.context_generation = 7;

    // Reads carry the generation the mutation must echo.
    const bridge::Response read = app::HandleAgentRequest(MakeRequest("get_case_overview"), context);
    ASSERT_FALSE(read.result.value("isError", true)) << read.result.dump();
    EXPECT_EQ(read.result["context_generation"], 7);

    // Missing: refused, and the refusal names the argument.
    const bridge::Response missing = app::HandleAgentRequest(
        MakeRequest("begin_change_group",
                    {{"title", "Needs generation"}, {"rationale", "r"}, {"expected_working_revision", 0}}),
        context);
    ASSERT_TRUE(missing.result.value("isError", false)) << missing.result.dump();
    EXPECT_NE(missing.result.value("error", std::string()).find("expected_context_generation"), std::string::npos);

    // Stale: the context changed identity since the client read it.
    const bridge::Response stale = app::HandleAgentRequest(MakeRequest("begin_change_group",
                                                                       {{"title", "Stale generation"},
                                                                        {"rationale", "r"},
                                                                        {"expected_working_revision", 0},
                                                                        {"expected_context_generation", 6}}),
                                                           context);
    ASSERT_TRUE(stale.result.value("isError", false)) << stale.result.dump();
    EXPECT_NE(stale.result.value("error", std::string()).find("context changed"), std::string::npos);

    // Current: proceeds, and the envelope names the generation back.
    const bridge::Response begun = app::HandleAgentRequest(MakeRequest("begin_change_group",
                                                                       {{"title", "Fresh generation"},
                                                                        {"rationale", "r"},
                                                                        {"expected_working_revision", 0},
                                                                        {"expected_context_generation", 7}}),
                                                           context);
    ASSERT_TRUE(begun.ok);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
    EXPECT_EQ(begun.result["context_generation"], 7);
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

    // The unsupported claim just staged is reported with the catalog's stable
    // check id on the wire, the key an agent deduplicates findings on across
    // staging calls rather than re-reading the sentence each time.
    ASSERT_TRUE(staged.result.contains("findings")) << staged.result.dump();
    bool unsupported_claim_reported = false;
    for (const nlohmann::json& finding : staged.result["findings"]) {
        if (finding.value("guideline_id", "") == "EV.1" && finding.value("element_id", "") == created_id) {
            unsupported_claim_reported = true;
            EXPECT_EQ(finding.value("check_id", ""), "check-evidence-trace") << finding.dump();
        }
    }
    EXPECT_TRUE(unsupported_claim_reported) << staged.result.dump();

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

// Terminology goes through the same change groups as argument edits: a staged
// term is visible to reads in the working-draft view, revision-checked, and
// never touches the accepted model until a human promotes it.
TEST(AgentRequestHandler, TermsStageThroughChangeGroupsAndListTermsSeesThem) {
    TempDir workspace{UniqueTempPath("term-draft")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 44;
    context.source_session_id = "stable-mcp-session-44";
    context.current_argument_view = [&] {
        const core::drafts::DraftWorkspace* draft = drafts.workspace();
        if (draft == nullptr || !draft->has_active_groups())
            return app::AgentArgumentView{&state.loaded_case.value(), nullptr};
        const core::drafts::DraftMaterializationResult& materialized =
            drafts.Materialize(state.loaded_case.value(), state.case_revision);
        return app::AgentArgumentView{&materialized.working_model, draft, true};
    };

    // A fresh project defines no terminology, and the empty answer says so
    // rather than leaving the agent to wonder whether the tool works.
    const bridge::Response empty = app::HandleAgentRequest(MakeRequest("list_terms"), context);
    ASSERT_TRUE(empty.ok);
    ASSERT_FALSE(empty.result.value("isError", true)) << empty.result.dump();
    EXPECT_EQ(empty.result["count"], 0);
    EXPECT_TRUE(empty.result.contains("note"));

    const bridge::Response begun =
        app::HandleAgentRequest(MakeRequest("begin_change_group",
                                            {{"title", "Bound the term 'hazard'"},
                                             {"rationale", "CL.5: claims use 'hazard' without a bound."},
                                             {"expected_working_revision", 0}}),
                                context);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
    const std::string group_id = begun.result["group_id"].get<std::string>();
    const std::uint64_t begun_revision = begun.result["working_revision"].get<std::uint64_t>();

    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", group_id},
                     {"expected_working_revision", begun_revision},
                     {"operations",
                      nlohmann::json::array(
                          {{{"type", "CreateTerm"},
                            {"create_ref", "$hazard"},
                            {"text", "hazard"},
                            {"new_value", "A system state that could lead to harm in the operating context."}}})}}),
        context);
    ASSERT_TRUE(staged.ok);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();
    const std::string term_id = staged.result["created_element_ids"]["$hazard"].get<std::string>();

    // The staged term reads back through the working-draft view with its
    // term-domain fields, not the flat model's storage names.
    const bridge::Response listed = app::HandleAgentRequest(MakeRequest("list_terms"), context);
    ASSERT_FALSE(listed.result.value("isError", true)) << listed.result.dump();
    EXPECT_EQ(listed.result["view"], "working_draft");
    ASSERT_EQ(listed.result["count"], 1);
    EXPECT_EQ(listed.result["terms"][0]["id"], term_id);
    EXPECT_EQ(listed.result["terms"][0]["value"], "hazard");
    EXPECT_EQ(listed.result["terms"][0]["definition"],
              "A system state that could lead to harm in the operating context.");

    EXPECT_EQ(parser::FindElementById(state.loaded_case.value(), term_id), nullptr)
        << "staging a term through MCP must not mutate the accepted model";
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

TEST(AgentRequestHandler, LegacyListChangeSetsReturnsChangeSetsAlias) {
    TempDir workspace{UniqueTempPath("legacy-list-change-sets")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "Legacy MCP client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 9;
    const bridge::Response begun = app::HandleAgentRequest(
        MakeRequest("begin_change_set", {{"title", "Legacy group"}, {"expected_working_revision", 0}}), context);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();

    const bridge::Response listed = app::HandleAgentRequest(MakeRequest("list_change_sets"), context);
    ASSERT_FALSE(listed.result.value("isError", true)) << listed.result.dump();
    ASSERT_TRUE(listed.result.contains("groups"));
    ASSERT_TRUE(listed.result.contains("change_sets"));
    EXPECT_EQ(listed.result["change_sets"], listed.result["groups"]);
    ASSERT_EQ(listed.result["change_sets"].size(), 1u);
    EXPECT_EQ(listed.result["change_sets"][0]["change_set_id"], begun.result["change_set_id"]);
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

// A client asked for a safety case in English and Japanese has to be able to
// state both over the wire, read both back, and find an element by either. The
// path here is the whole one: JSON arguments to a persisted draft group to a
// materialized working model to the JSON the next read returns.
TEST(AgentRequestHandler, StagesAndReadsBackAClaimInTwoLanguages) {
    TempDir workspace{UniqueTempPath("bilingual")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 42;
    context.source_session_id = "stable-mcp-session-42";
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
                                            {{"title", "Add a bilingual sub-claim"},
                                             {"rationale", "The case is reviewed in both languages."},
                                             {"expected_working_revision", 0}}),
                                context);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
    const std::string group_id = begun.result["group_id"].get<std::string>();

    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest(
            "stage_operations",
            {{"group_id", group_id},
             {"expected_working_revision", begun.result["working_revision"].get<std::uint64_t>()},
             {"operations",
              nlohmann::json::array({{{"type", "CreateClaim"},
                                      {"create_ref", "$monitoring"},
                                      {"text", "Monitoring detects unsafe blender operation"},
                                      {"translations", {{"ja", "監視により危険なブレンダー動作を検出する。"}}}}})}}),
        context);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();
    const std::string created_id = staged.result["created_element_ids"]["$monitoring"].get<std::string>();

    // Echoed back, so a client can see what it staged rather than assume it.
    ASSERT_TRUE(staged.result.contains("operations"));
    EXPECT_EQ(staged.result["operations"][0]["translations"]["ja"], "監視により危険なブレンダー動作を検出する。");

    const bridge::Response read = app::HandleAgentRequest(MakeRequest("get_element", {{"id", created_id}}), context);
    ASSERT_FALSE(read.result.value("isError", true)) << read.result.dump();
    EXPECT_EQ(read.result["element"]["content"], "Monitoring detects unsafe blender operation");
    EXPECT_EQ(read.result["element"]["translations"]["content"]["ja"], "監視により危険なブレンダー動作を検出する。");
    ASSERT_TRUE(read.result["element"].contains("translated_languages"));
    EXPECT_EQ(read.result["element"]["translated_languages"], nlohmann::json::array({"ja"}));

    // Searchable in the language it was written in. Searching only the primary
    // would report that a claim does not exist because the user asked for it in
    // the language the claim is not indexed in.
    const bridge::Response found =
        app::HandleAgentRequest(MakeRequest("find_elements", {{"query", "ブレンダー動作"}}), context);
    ASSERT_FALSE(found.result.value("isError", true)) << found.result.dump();
    ASSERT_EQ(found.result["matches"].size(), 1u) << found.result.dump();
    EXPECT_EQ(found.result["matches"][0]["id"], created_id);
    EXPECT_EQ(found.result["matches"][0]["translated_languages"], nlohmann::json::array({"ja"}));
}

TEST(AgentRequestHandler, RefusesAnOperationWhoseTranslationsAreNotAMapOfText) {
    TempDir workspace{UniqueTempPath("bad-translations")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 43;
    context.source_session_id = "stable-mcp-session-43";

    const bridge::Response begun = app::HandleAgentRequest(
        MakeRequest("begin_change_group", {{"title", "Malformed"}, {"expected_working_revision", 0}}), context);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();

    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", begun.result["group_id"].get<std::string>()},
                     {"expected_working_revision", begun.result["working_revision"].get<std::uint64_t>()},
                     {"operations",
                      nlohmann::json::array({{{"type", "CreateClaim"},
                                              {"create_ref", "$x"},
                                              {"text", "An English claim"},
                                              {"translations", "ja"}}})}}),
        context);

    // Dropping the malformed field instead would tell the client its bilingual
    // claim was staged, and the Japanese would simply not be there.
    EXPECT_TRUE(staged.result.value("isError", false)) << staged.result.dump();
}

// Phase 3 of docs/features/mcp-authoring-quality-plan.md: rehearsal without
// staging. The findings are the ones staging would return; the store, the
// revision and the user's canvas are untouched.
TEST(AgentRequestHandler, CheckOperationsRehearsesWithoutStoringAnything) {
    TempDir workspace{UniqueTempPath("check-ops")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 44;
    context.source_session_id = "stable-mcp-session-44";

    // An unsupported claim: the rehearsal must report EV.1 exactly as staging
    // would, with the catalog check id on the wire.
    const bridge::Response checked = app::HandleAgentRequest(
        MakeRequest(
            "check_operations",
            {{"operations",
              nlohmann::json::array(
                  {{{"type", "CreateClaim"}, {"create_ref", "$rehearsed"}, {"text", "Rehearsed and unsupported"}}})}}),
        context);
    ASSERT_TRUE(checked.ok) << checked.error_message;
    ASSERT_FALSE(checked.result.value("isError", true)) << checked.result.dump();
    EXPECT_TRUE(checked.result.value("materializes", false));
    EXPECT_TRUE(checked.result.contains("rehearsal"));
    EXPECT_FALSE(checked.result.contains("created_element_ids")) << checked.result.dump();
    bool unsupported_reported = false;
    for (const nlohmann::json& finding : checked.result["findings"]) {
        if (finding.value("check_id", "") == "check-evidence-trace")
            unsupported_reported = true;
    }
    EXPECT_TRUE(unsupported_reported) << checked.result.dump();

    // Nothing was stored: no workspace was created and the revision never moved.
    EXPECT_EQ(drafts.workspace(), nullptr);
    EXPECT_EQ(drafts.revision(), 0u);

    // And the store is still perfectly willing to stage for real afterwards.
    const bridge::Response begun = app::HandleAgentRequest(
        MakeRequest("begin_change_group", {{"title", "For real"}, {"expected_working_revision", 0}}), context);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
}

// The submit gate: a group with standing problem-severity findings is refused
// until its author fixes them or explicitly acknowledges them, and the
// acknowledgment is recorded on the group for the reviewer.
TEST(AgentRequestHandler, SubmitRefusesStandingProblemFindingsUntilAcknowledged) {
    TempDir workspace{UniqueTempPath("submit-gate")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 45;
    context.source_session_id = "stable-mcp-session-45";

    const bridge::Response claims =
        app::HandleAgentRequest(MakeRequest("find_elements", {{"type", "claim"}, {"limit", 1}}), context);
    ASSERT_FALSE(claims.result.value("isError", true)) << claims.result.dump();
    ASSERT_FALSE(claims.result["matches"].empty());
    const std::string top_goal = claims.result["matches"][0]["id"].get<std::string>();

    const bridge::Response begun = app::HandleAgentRequest(
        MakeRequest("begin_change_group", {{"title", "Empty strategy"}, {"expected_working_revision", 0}}), context);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
    const std::string group_id = begun.result["group_id"].get<std::string>();

    // A strategy that develops into nothing: AR.1 role misuse, Problem severity.
    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest(
            "stage_operations",
            {{"group_id", group_id},
             {"expected_working_revision", begun.result["working_revision"].get<std::uint64_t>()},
             {"operations",
              nlohmann::json::array(
                  {{{"type", "CreateStrategy"}, {"create_ref", "$s"}, {"text", "Argue over hazards"}},
                   {{"type", "AddSupportedBy"}, {"source", {{"ref", "$s"}}}, {"target", {{"id", top_goal}}}}})}}),
        context);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();
    const std::uint64_t staged_revision = staged.result["working_revision"].get<std::uint64_t>();

    // Submit is refused, the refusal names the findings, and nothing moved.
    const bridge::Response refused = app::HandleAgentRequest(
        MakeRequest("submit_change_group", {{"group_id", group_id}, {"expected_working_revision", staged_revision}}),
        context);
    ASSERT_TRUE(refused.result.value("isError", false)) << refused.result.dump();
    ASSERT_TRUE(refused.result.contains("problem_findings")) << refused.result.dump();
    EXPECT_FALSE(refused.result["problem_findings"].empty());
    EXPECT_NE(refused.result.value("error", std::string()).find("acknowledge_findings"), std::string::npos);
    EXPECT_EQ(drafts.revision(), staged_revision);

    const bridge::Response still_building =
        app::HandleAgentRequest(MakeRequest("describe_change_group", {{"group_id", group_id}}), context);
    EXPECT_EQ(still_building.result.value("state", ""), "building") << still_building.result.dump();

    // Acknowledged: the group goes ready and carries the record.
    const bridge::Response submitted = app::HandleAgentRequest(
        MakeRequest(
            "submit_change_group",
            {{"group_id", group_id}, {"expected_working_revision", staged_revision}, {"acknowledge_findings", true}}),
        context);
    ASSERT_FALSE(submitted.result.value("isError", true)) << submitted.result.dump();
    EXPECT_EQ(submitted.result.value("state", ""), "ready");
    ASSERT_TRUE(submitted.result.contains("acknowledged_findings")) << submitted.result.dump();
    // The empty strategy is AR.1 role misuse: the element is not doing the job
    // its role names. AR.2's check is on a decomposition with no reasoning step.
    bool acknowledged_role_misuse = false;
    for (const nlohmann::json& entry : submitted.result["acknowledged_findings"]) {
        if (entry.get<std::string>().find("AR.1") != std::string::npos)
            acknowledged_role_misuse = true;
    }
    EXPECT_TRUE(acknowledged_role_misuse) << submitted.result.dump();
}

// An agent was given the tool's paraphrase of a rule and no way to reach the
// rule. A reviewer is never asked to accept that, and an agent proposing
// changes to a safety argument should not be either.
TEST(AgentRequestHandler, EverySccgFindingCarriesTheRuleAndAPointerToIt) {
    TempDir workspace{UniqueTempPath("finding-rule")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 71;
    context.source_session_id = "stable-mcp-session-71";

    const bridge::Response claims =
        app::HandleAgentRequest(MakeRequest("find_elements", {{"type", "claim"}, {"limit", 1}}), context);
    ASSERT_FALSE(claims.result["matches"].empty());
    const std::string top_goal = claims.result["matches"][0]["id"].get<std::string>();

    const bridge::Response begun = app::HandleAgentRequest(
        MakeRequest("begin_change_group", {{"title", "Empty strategy"}, {"expected_working_revision", 0}}), context);
    const std::string group_id = begun.result["group_id"].get<std::string>();

    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest(
            "stage_operations",
            {{"group_id", group_id},
             {"expected_working_revision", begun.result["working_revision"].get<std::uint64_t>()},
             {"operations",
              nlohmann::json::array(
                  {{{"type", "CreateStrategy"}, {"create_ref", "$s"}, {"text", "Argue over hazards"}},
                   {{"type", "AddSupportedBy"}, {"source", {{"ref", "$s"}}}, {"target", {{"id", top_goal}}}}})}}),
        context);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();
    ASSERT_TRUE(staged.result.contains("findings")) << staged.result.dump();

    bool saw_sccg = false;
    for (const nlohmann::json& finding : staged.result["findings"]) {
        if (finding.value("kind", "") != "sccg")
            continue;
        saw_sccg = true;
        EXPECT_FALSE(finding.value("statement", "").empty()) << "the guideline's own wording is missing";
        EXPECT_EQ(finding.value("guideline_uri", ""),
                  "sccg://guideline/" + finding.value("guideline_id", std::string{}));
    }
    EXPECT_TRUE(saw_sccg) << staged.result.dump();

    // An empty findings array said nothing about how much of SCCG was examined,
    // so an agent reading one could conclude its argument conformed. It does not.
    ASSERT_TRUE(staged.result.contains("checked")) << staged.result.dump();
    const nlohmann::json& checked = staged.result["checked"];
    ASSERT_TRUE(checked.contains("mechanical_checks"));
    EXPECT_FALSE(checked["mechanical_checks"].empty());
    EXPECT_NE(checked.value("note", "").find("does not mean"), std::string::npos)
        << "the result must say plainly that no findings is not conformance";
}

// Reported from a real session: an agent asked to build a glossary staged six
// terms, every call succeeded, and the accepted argument came out with six
// terms carrying a word and no definition. The definitions had been sent under
// a key the parser does not read, and staging reported success over the drop.
// The batch is refused now, and the refusal says where a definition belongs.
TEST(AgentRequestHandler, StagingATermWhoseDefinitionUsesAnUnreadKeyIsRefused) {
    TempDir workspace{UniqueTempPath("term-definition-key")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 63;
    context.source_session_id = "stable-mcp-session-63";
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
                                            {{"title", "Define the terms the argument leans on"},
                                             {"rationale", "The claims use terms of art that are nowhere defined."},
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
                      nlohmann::json::array({{{"type", "CreateTerm"},
                                              {"create_ref", "$alarp"},
                                              {"text", "ALARP"},
                                              {"definition", "As low as reasonably practicable."}}})}}),
        context);

    ASSERT_TRUE(staged.ok);
    EXPECT_TRUE(staged.result.value("isError", false))
        << "a definition the tool will not store must not be reported as staged: " << staged.result.dump();
    const std::string message = staged.result.dump();
    EXPECT_NE(message.find("definition"), std::string::npos) << message;
    EXPECT_NE(message.find("new_value"), std::string::npos)
        << "the refusal must say where the definition belongs: " << message;

    // Nothing staged, so nothing to accept: the group is exactly as it was.
    const bridge::Response described =
        app::HandleAgentRequest(MakeRequest("describe_change_group", {{"group_id", group_id}}), context);
    ASSERT_TRUE(described.ok);
    EXPECT_EQ(described.result.value("operation_count", -1), 0) << described.result.dump();
}

// A term is matched against element text by its value, so a value that appears
// nowhere bounds nothing. Reported from a real session: an agent staged
// `EPB (Electronic Parking Brake)` for text that says `EPB`, and the glossary
// looked right while resolving for no occurrence in the argument.
TEST(AgentRequestHandler, ListTermsReportsATermWhoseValueAppearsNowhere) {
    TempDir workspace{UniqueTempPath("term-matches-nothing")};
    core::AppState state;
    ASSERT_TRUE(OpenProjectWithArgument(state, workspace.path));

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(workspace.path);
    std::string error;
    ASSERT_TRUE(drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) << error;

    app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
    context.draft_workspace = &drafts;
    context.connection_id = 71;
    context.source_session_id = "stable-mcp-session-71";
    context.current_argument_view = [&] {
        const core::drafts::DraftWorkspace* draft = drafts.workspace();
        if (draft == nullptr || !draft->has_active_groups())
            return app::AgentArgumentView{&state.loaded_case.value(), nullptr};
        const core::drafts::DraftMaterializationResult& materialized =
            drafts.Materialize(state.loaded_case.value(), state.case_revision);
        return app::AgentArgumentView{&materialized.working_model, draft, true};
    };

    const bridge::Response begun = app::HandleAgentRequest(
        MakeRequest("begin_change_group",
                    {{"title", "Define the abbreviations"},
                     {"rationale", "The claims lean on abbreviations that are nowhere bounded."},
                     {"expected_working_revision", 0}}),
        context);
    ASSERT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
    const std::string group_id = begun.result["group_id"].get<std::string>();
    const std::uint64_t begun_revision = begun.result["working_revision"].get<std::uint64_t>();

    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", group_id},
                     {"expected_working_revision", begun_revision},
                     {"operations",
                      nlohmann::json::array({{{"type", "CreateTerm"},
                                              {"create_ref", "$epb"},
                                              {"text", "EPB (Electronic Parking Brake)"},
                                              {"new_value", "The electrically actuated parking brake."}}})}}),
        context);
    ASSERT_TRUE(staged.ok);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();

    const bridge::Response listed = app::HandleAgentRequest(MakeRequest("list_terms"), context);
    ASSERT_FALSE(listed.result.value("isError", true)) << listed.result.dump();
    ASSERT_EQ(listed.result["count"], 1);
    EXPECT_TRUE(listed.result["terms"][0].value("matches_no_text", false))
        << "a value that appears nowhere in the argument must be reported: " << listed.result.dump();
    ASSERT_TRUE(listed.result.contains("note")) << listed.result.dump();
    const std::string note = listed.result["note"].get<std::string>();
    EXPECT_NE(note.find("appears nowhere"), std::string::npos) << note;
    EXPECT_NE(note.find("expansion"), std::string::npos) << "the note must say where the expansion goes: " << note;
}
