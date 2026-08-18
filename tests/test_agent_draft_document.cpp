#include "app/agent_request_handler.h"

#include "core/app_state.h"
#include "core/drafts/draft_document_diff.h"
#include "core/drafts/draft_document_store.h"
#include "core/project_file_io.h"
#include "parser/model_utils.h"
#include "sacm_adapter/case_projection.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

// What a connected MCP client actually drives, once staging reaches the draft
// document rather than an operation log (ADR 0016).
//
// The property under test throughout is the one the operation-staging path could
// not hold: a change is accepted or refused by the model that will have to hold
// it, in the call that made it. A client is never told "staged" about something
// only the accept will discover it cannot do -- and never told "withdrawn" about
// something the draft is still holding.

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
        std::filesystem::temp_directory_path() / ("af_agent_draft_doc_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

bridge::Request MakeRequest(const std::string& op, const nlohmann::json& args = {}) {
    bridge::Request request;
    request.id = 11;
    request.op = op;
    request.args = args.is_object() ? args : nlohmann::json::object();
    return request;
}

// A project with an argument open, a draft workspace and a draft document --
// which is what the runtime hands the handler when a client connects to a
// running application with a project loaded.
struct ConnectedProject {
    TempDir workspace;
    core::AppState state;
    core::drafts::DraftWorkspaceStore drafts;
    core::drafts::DraftDocumentStore document;

    bool Open(const std::string& stem) {
        workspace.path = UniqueTempPath(stem);
        if (!state.create_empty_project("Project", workspace.path.string())) {
            ADD_FAILURE() << "could not create project: " << state.status_message;
            return false;
        }
        bool opened = false;
        for (const core::ProjectFileEntry& entry : state.current_project->files) {
            if (entry.role == core::ProjectFileRole::SacmArgument) {
                opened = state.open_project_file(entry);
                break;
            }
        }
        if (!opened) {
            ADD_FAILURE() << "seeded project holds no openable SACM argument";
            return false;
        }
        if (state.library_document == nullptr) {
            ADD_FAILURE() << "the seeded argument has no library document to draft from";
            return false;
        }

        drafts.SetProjectRoot(workspace.path);
        std::string error;
        if (!drafts.Open(state.loaded_file_path, state.loaded_case.value(), error)) {
            ADD_FAILURE() << "draft workspace: " << error;
            return false;
        }
        if (!document.Open(workspace.path, state.loaded_file_path, *state.library_document, error)) {
            ADD_FAILURE() << "draft document: " << error;
            return false;
        }
        return true;
    }

    app::AgentRequestContext Context() {
        app::AgentRequestContext context{state, workspace.path.string(), "MCP test client", {}};
        context.draft_workspace = &drafts;
        context.draft_document = &document;
        context.connection_id = 3;
        context.source_session_id = "draft-document-session";
        context.current_argument_view = [this] {
            if (!state.loaded_case.has_value())
                return app::AgentArgumentView{};
            // Mirrors the runtime: the draft is the working argument whenever it
            // differs from the accepted one, and the label follows the content
            // rather than the existence of a change group.
            view_projection = document.Projection();
            const bool differs =
                core::drafts::DiffAcceptedAgainstDraft(state.loaded_case.value(), view_projection).touches_anything();
            if (!differs)
                return app::AgentArgumentView{&state.loaded_case.value(), drafts.workspace(), false};
            return app::AgentArgumentView{&view_projection, drafts.workspace(), true};
        };
        return context;
    }

    core::AssuranceCase view_projection;
};

// The first claim in the seeded argument -- the element these tests hang their
// operations off, so they address something the application really produced.
std::string FirstClaimId(const core::AssuranceCase& model) {
    for (const core::SacmElement& element : model.elements) {
        if (element.type == "claim")
            return element.id;
    }
    return {};
}

// Opens a group and returns (group_id, working_revision).
std::pair<std::string, std::uint64_t> BeginGroup(const app::AgentRequestContext& context, std::uint64_t revision) {
    const bridge::Response begun = app::HandleAgentRequest(
        MakeRequest("begin_change_group",
                    {{"title", "Strengthen the monitoring argument"}, {"expected_working_revision", revision}}),
        context);
    EXPECT_TRUE(begun.ok) << begun.error_message;
    EXPECT_FALSE(begun.result.value("isError", true)) << begun.result.dump();
    if (begun.result.value("isError", true))
        return {};
    return {begun.result["group_id"].get<std::string>(), begun.result["working_revision"].get<std::uint64_t>()};
}

std::uint64_t CurrentRevision(const app::AgentRequestContext& context) {
    const bridge::Response status = app::HandleAgentRequest(MakeRequest("get_draft_status"), context);
    EXPECT_FALSE(status.result.value("isError", true)) << status.result.dump();
    return status.result["working_revision"].get<std::uint64_t>();
}

} // namespace

// The whole point of the switchover, end to end: a client's operation reaches
// the draft document, is readable back through the same session, and leaves the
// accepted safety argument untouched on disk.
TEST(AgentDraftDocument, StagedOperationsReachTheDraftDocumentAndNotTheAcceptedArgument) {
    ConnectedProject project;
    ASSERT_TRUE(project.Open("staging"));
    const app::AgentRequestContext context = project.Context();

    const std::filesystem::path accepted_path = project.state.loaded_file_path;
    const std::expected<std::string, std::string> before = core::ReadTextFile(accepted_path);
    ASSERT_TRUE(before.has_value()) << before.error();

    const std::string anchor = FirstClaimId(project.state.loaded_case.value());
    ASSERT_FALSE(anchor.empty());

    const auto [group_id, revision] = BeginGroup(context, CurrentRevision(context));
    ASSERT_FALSE(group_id.empty());

    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", group_id},
                     {"expected_working_revision", revision},
                     {"anchor_element_id", anchor},
                     {"operations",
                      nlohmann::json::array({{{"type", "CreateClaim"},
                                              {"create_ref", "$monitoring"},
                                              {"text", "Monitoring detects unsafe blender operation"}},
                                             {{"type", "AddSupportedBy"},
                                              {"source", {{"id", anchor}}},
                                              {"target", {{"ref", "$monitoring"}}}}})}}),
        context);
    ASSERT_TRUE(staged.ok) << staged.error_message;
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();

    // A real id, allocated by the document. Nothing later reallocates it, so the
    // client can address the element it just made in its very next call.
    ASSERT_TRUE(staged.result.contains("created_element_ids"));
    const std::string created = staged.result["created_element_ids"]["$monitoring"].get<std::string>();
    ASSERT_FALSE(created.empty());
    EXPECT_GE(staged.result["added"].get<int>(), 1);

    const bridge::Response read = app::HandleAgentRequest(MakeRequest("get_element", {{"id", created}}), context);
    ASSERT_FALSE(read.result.value("isError", true)) << read.result.dump();
    EXPECT_EQ(read.result["element"]["content"], "Monitoring detects unsafe blender operation");
    // The client must never have to guess whether what it just read is accepted.
    EXPECT_EQ(read.result["view"], "working_draft");

    EXPECT_EQ(parser::FindElementById(project.state.loaded_case.value(), created), nullptr)
        << "staging must not mutate the accepted model";
    const std::expected<std::string, std::string> after = core::ReadTextFile(accepted_path);
    ASSERT_TRUE(after.has_value()) << after.error();
    EXPECT_EQ(after.value(), before.value()) << "the accepted .sacm must be byte-identical until a human accepts";

    // Persisted, so the conversation survives a restart.
    EXPECT_TRUE(std::filesystem::exists(project.document.path()));
}

// The defect class ADR 0016 exists for. A relationship endpoint the library
// refuses has to be refused here, in the call that named it -- not accepted into
// a draft, drawn on the canvas as pending, and refused at accept.
TEST(AgentDraftDocument, AnOperationTheDocumentCannotHoldIsRefusedWhenItIsMade) {
    ConnectedProject project;
    ASSERT_TRUE(project.Open("refusal"));
    const app::AgentRequestContext context = project.Context();

    const std::string anchor = FirstClaimId(project.state.loaded_case.value());
    ASSERT_FALSE(anchor.empty());
    const auto [group_id, revision] = BeginGroup(context, CurrentRevision(context));
    ASSERT_FALSE(group_id.empty());

    const bridge::Response refused = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", group_id},
                     {"expected_working_revision", revision},
                     {"operations",
                      nlohmann::json::array(
                          {{{"type", "CreateClaim"}, {"create_ref", "$first"}, {"text", "This one is expressible"}},
                           {{"type", "AddSupportedBy"},
                            {"source", {{"id", anchor}}},
                            {"target", {{"id", "no-such-element"}}}}})}}),
        context);
    ASSERT_TRUE(refused.ok) << refused.error_message;
    EXPECT_TRUE(refused.result.value("isError", false)) << refused.result.dump();
    // Positioned, so a client staging a batch is told which operation to fix
    // rather than having to re-send it blind.
    EXPECT_NE(refused.result["error"].get<std::string>().find("Operation 2"), std::string::npos)
        << refused.result.dump();

    // Atomic: the expressible first operation must not have survived the batch.
    const core::drafts::DraftDocumentDiff diff =
        core::drafts::DiffAcceptedAgainstDraft(project.state.loaded_case.value(), project.document.Projection());
    EXPECT_FALSE(diff.touches_anything()) << "a refused batch must leave the draft exactly as it was";
}

// A gesture that only means something against an operation log must not report
// success over a change the draft is still holding. That silent-drop shape is
// what the redesign removed; it must not survive in a different call.
TEST(AgentDraftDocument, WithdrawingOperationsIsRefusedRatherThanReportedOverAChangeThatRemains) {
    ConnectedProject project;
    ASSERT_TRUE(project.Open("unstage"));
    const app::AgentRequestContext context = project.Context();

    const std::string anchor = FirstClaimId(project.state.loaded_case.value());
    ASSERT_FALSE(anchor.empty());
    const auto [group_id, revision] = BeginGroup(context, CurrentRevision(context));
    ASSERT_FALSE(group_id.empty());

    const bridge::Response staged =
        app::HandleAgentRequest(MakeRequest("stage_operations",
                                            {{"group_id", group_id},
                                             {"expected_working_revision", revision},
                                             {"operations",
                                              nlohmann::json::array({{{"type", "UpdateElementName"},
                                                                      {"element", {{"id", anchor}}},
                                                                      {"new_value", "A renamed top goal"}}})}}),
                                context);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();

    for (const char* gesture : {"unstage_operations", "remove_change_group", "replace_change_group"}) {
        const bridge::Response response = app::HandleAgentRequest(
            MakeRequest(gesture,
                        {{"group_id", group_id},
                         {"expected_working_revision", CurrentRevision(context)},
                         {"count", 1},
                         {"operations",
                          nlohmann::json::array({{{"type", "UpdateElementName"},
                                                  {"element", {{"id", anchor}}},
                                                  {"new_value", "Something else entirely"}}})}}),
            context);
        EXPECT_TRUE(response.result.value("isError", false)) << gesture << ": " << response.result.dump();
        EXPECT_NE(response.result["error"].get<std::string>().find("SACM document"), std::string::npos)
            << gesture << ": the refusal must say why, and what to do instead";
    }

    // And the change really is still there, which is what makes reporting these
    // as successes a lie rather than a nicety.
    const core::drafts::DraftDocumentDiff diff =
        core::drafts::DiffAcceptedAgainstDraft(project.state.loaded_case.value(), project.document.Projection());
    ASSERT_NE(diff.Find(anchor), nullptr);
    EXPECT_EQ(diff.Find(anchor)->change, core::drafts::DraftElementChange::Modified);
}

// The staleness token has to move when the draft does, whoever moved it. A user
// editing the argument edits the draft (ADR 0016), so a client watching only the
// change-group ledger would compute against an argument that had moved.
TEST(AgentDraftDocument, TheWorkingRevisionMovesWhenTheDocumentMovesUnderTheClient) {
    ConnectedProject project;
    ASSERT_TRUE(project.Open("revision"));
    const app::AgentRequestContext context = project.Context();

    const std::string anchor = FirstClaimId(project.state.loaded_case.value());
    ASSERT_FALSE(anchor.empty());
    const auto [group_id, revision] = BeginGroup(context, CurrentRevision(context));
    ASSERT_FALSE(group_id.empty());

    // The first staged edit is what brings the draft into existence.
    const bridge::Response staged =
        app::HandleAgentRequest(MakeRequest("stage_operations",
                                            {{"group_id", group_id},
                                             {"expected_working_revision", revision},
                                             {"operations",
                                              nlohmann::json::array({{{"type", "UpdateElementName"},
                                                                      {"element", {{"id", anchor}}},
                                                                      {"new_value", "A first contribution"}}})}}),
                                context);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();
    const std::uint64_t after_staging = CurrentRevision(context);

    // Stands in for the user editing the argument in the application: their
    // edits go into the same draft, so the token moves with no change group
    // touched at all.
    project.document.MarkChanged();
    EXPECT_GT(CurrentRevision(context), after_staging);

    const bridge::Response stale = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", group_id},
                     {"expected_working_revision", after_staging},
                     {"operations",
                      nlohmann::json::array({{{"type", "UpdateElementName"},
                                              {"element", {{"id", anchor}}},
                                              {"new_value", "Written against a draft that has moved"}}})}}),
        context);
    EXPECT_TRUE(stale.result.value("isError", false)) << stale.result.dump();
    EXPECT_EQ(stale.result["current_working_revision"].get<std::uint64_t>(), CurrentRevision(context));
}

// Accept, from the client's contribution to the accepted file. The accept itself
// is a single atomic write, so what this covers is the chain around it: what a
// client staged is what the accepted argument ends up holding, and the draft is
// gone afterwards rather than left to be applied twice.
TEST(AgentDraftDocument, AcceptingWritesWhatTheClientStagedIntoTheAcceptedArgument) {
    ConnectedProject project;
    ASSERT_TRUE(project.Open("accept"));
    const app::AgentRequestContext context = project.Context();

    const std::string anchor = FirstClaimId(project.state.loaded_case.value());
    ASSERT_FALSE(anchor.empty());
    const auto [group_id, revision] = BeginGroup(context, CurrentRevision(context));
    ASSERT_FALSE(group_id.empty());

    const bridge::Response staged = app::HandleAgentRequest(
        MakeRequest("stage_operations",
                    {{"group_id", group_id},
                     {"expected_working_revision", revision},
                     {"anchor_element_id", anchor},
                     {"operations",
                      nlohmann::json::array({{{"type", "CreateClaim"},
                                              {"create_ref", "$monitoring"},
                                              {"text", "Monitoring detects unsafe blender operation"}}})}}),
        context);
    ASSERT_FALSE(staged.result.value("isError", true)) << staged.result.dump();
    const std::string created = staged.result["created_element_ids"]["$monitoring"].get<std::string>();

    const std::filesystem::path accepted_path = project.state.loaded_file_path;
    std::string error;
    ASSERT_TRUE(project.document.AcceptInto(accepted_path, error)) << error;

    // The accepted file is now the draft, and re-reading it is how the
    // application learns that -- the `.sacm` is the source of truth, not a model
    // held beside it.
    ASSERT_TRUE(project.state.load_file(accepted_path.string())) << project.state.status_message;
    EXPECT_NE(parser::FindElementById(project.state.loaded_case.value(), created), nullptr)
        << "the accepted argument must hold what the human approved";

    // Gone, so nothing can apply it a second time.
    EXPECT_FALSE(project.document.active());
    EXPECT_FALSE(std::filesystem::exists(project.document.path()));

    // And reopening the argument finds no draft at all, which is what "there is
    // no unaccepted work left" looks like -- not an empty one sitting beside the
    // argument waiting to go stale against the user's next edit.
    core::drafts::DraftDocumentStore reopened;
    ASSERT_TRUE(reopened.Open(project.workspace.path, accepted_path, *project.state.library_document, error)) << error;
    EXPECT_FALSE(reopened.active()) << "the accept consumed the draft; nothing should remain to reopen";
}
