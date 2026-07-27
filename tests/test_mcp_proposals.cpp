#include "mcp/server.h"

#include "mcp/session.h"
#include "mcp/tools.h"

#include "core/app_state.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_item_manager.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_patch_service.h"
#include "parser/model_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

// The MCP write surface. An agent cannot edit the case: it writes ReviewProposal
// drafts a human accepts in the GUI. These pin that the drafts are real (they
// round-trip through the same manager the GUI reads), that nothing invalid
// reaches disk, and that a preview writes nothing at all.

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
        std::filesystem::temp_directory_path() /
        ("af_mcp_proposals_" + stem + "_" + std::to_string(++counter));
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
    TempDir                       workspace;
    std::filesystem::path         project_root;
    std::unique_ptr<mcp::Session> session;
};

// Builds a real project on disk (the seed document carries one claim), then
// opens an MCP session against it.
std::unique_ptr<Fixture> MakeProjectFixture(const std::string& stem) {
    std::unique_ptr<Fixture> fixture(new Fixture{TempDir{UniqueTempPath(stem)}, {}, {}});

    core::AppState builder;
    if (!builder.create_empty_project("Project", fixture->workspace.path.string())) {
        ADD_FAILURE() << "could not create project: " << builder.status_message;
        return nullptr;
    }
    fixture->project_root = builder.current_project->rootPath;

    mcp::Session::Config config;
    config.project_path  = fixture->project_root;
    config.settings_path = WriteConsentingSettings(fixture->workspace.path);

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
    const std::optional<nlohmann::json> response = server.HandleMessage(
        Request("initialize", {{"clientInfo", {{"name", "TestClient"}, {"version", "2.0"}}}}, 1));
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
        server.HandleMessage(Request("tools/call", {{"name", name}, {"arguments", arguments}}, 9));
    if (!response.has_value() || !response->contains("result")) {
        ADD_FAILURE() << "tools/call returned no result: "
                      << (response.has_value() ? response->dump() : "<none>");
        return {};
    }
    const nlohmann::json& result = (*response)["result"];
    ToolCall              call;
    call.is_error = result.value("isError", false);
    call.payload  = nlohmann::json::parse(result["content"][0]["text"].get<std::string>(), nullptr,
                                          /*allow_exceptions=*/false);
    return call;
}

// The id of the claim the seeded project starts with, so tests attach to a real
// element rather than a hard-coded id the seed may later change.
std::string FirstClaimId(mcp::Server& server) {
    const ToolCall claims = CallTool(server, "find_elements", {{"type", "claim"}, {"limit", 1}});
    if (claims.is_error || claims.payload["matches"].empty()) {
        ADD_FAILURE() << "seeded project has no claim: " << claims.payload.dump();
        return {};
    }
    return claims.payload["matches"][0]["id"].get<std::string>();
}

nlohmann::json AddSubGoalOperations(const std::string& parent_id, const std::string& text) {
    return nlohmann::json::array(
        {nlohmann::json{{"type", "CreateClaim"}, {"create_ref", "$sub"}, {"text", text}},
         nlohmann::json{{"type", "AddSupportedBy"},
                        {"source", {{"id", parent_id}}},
                        {"target", {{"ref", "$sub"}}}}});
}

} // namespace

TEST(McpProposals, CreatesADraftThatRoundTripsThroughTheProposalManager) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("roundtrip");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);
    const std::string parent_id = FirstClaimId(server);
    ASSERT_FALSE(parent_id.empty());

    const ToolCall created = CallTool(
        server, "create_review_proposal",
        {{"title", "Decompose the top goal"},
         {"summary", "Adds a supporting sub-goal."},
         {"anchor_element_id", parent_id},
         {"operations", AddSubGoalOperations(parent_id, "Braking performance is adequate.")}});

    ASSERT_FALSE(created.is_error) << created.payload.dump();
    EXPECT_TRUE(created.payload["saved"].get<bool>());
    EXPECT_EQ(created.payload["element_count_after"].get<int>(),
              created.payload["element_count_before"].get<int>() + 2)
        << "expected the new claim plus its supporting relationship";

    // The GUI reads proposals through this manager, so a draft that does not
    // come back here is one the user would never see.
    const std::string proposal_id = created.payload["proposal_id"].get<std::string>();
    std::string       error;
    const std::optional<core::reviews::ReviewProposal> loaded =
        fixture->session->proposals().LoadProposal(proposal_id, error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_EQ(loaded->title, "Decompose the top goal");
    EXPECT_EQ(core::reviews::EvaluateReviewProposalValidity(*loaded, fixture->session->assurance_case())
                  .validity,
              core::reviews::ProposalValidity::Valid);
}

// The preview says what a proposal would do; this checks the file that was
// actually saved does it. Loading the draft back and applying it is the step
// between "a JSON file appeared" and "the user can accept a working change".
TEST(McpProposals, TheSavedDraftAppliesAndWiresTheNewGoalToItsParent) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("applies");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);
    const std::string parent_id = FirstClaimId(server);
    ASSERT_FALSE(parent_id.empty());

    const ToolCall created =
        CallTool(server, "create_review_proposal",
                 {{"title", "Decompose"},
                  {"anchor_element_id", parent_id},
                  {"operations", AddSubGoalOperations(parent_id, "Braking is adequate.")}});
    ASSERT_FALSE(created.is_error) << created.payload.dump();

    std::string error;
    const std::optional<core::reviews::ReviewProposal> loaded =
        fixture->session->proposals().LoadProposal(
            created.payload["proposal_id"].get<std::string>(), error);
    ASSERT_TRUE(loaded.has_value()) << error;

    parser::AssuranceCase model = fixture->session->assurance_case();
    const std::size_t     before = model.elements.size();

    const core::reviews::ReviewProposalPatchService service;
    const core::reviews::ApplyProposalResult       applied = service.ApplyProposal(*loaded, model);
    ASSERT_TRUE(applied.success) << applied.error;

    ASSERT_EQ(model.elements.size(), before + 2);
    const std::string new_id = applied.generated_ids.at("$sub");

    const parser::SacmElement* sub_goal = parser::FindElementById(model, new_id);
    ASSERT_NE(sub_goal, nullptr);
    EXPECT_EQ(sub_goal->type, "claim");
    EXPECT_EQ(sub_goal->content, "Braking is adequate.");

    // The relationship matters as much as the element: an unattached goal is not
    // a decomposition, it is a floating claim.
    bool linked = false;
    for (const parser::SacmElement& element : model.elements) {
        const bool from_parent = std::find(element.source_refs.begin(), element.source_refs.end(),
                                           parent_id) != element.source_refs.end();
        const bool to_new = std::find(element.target_refs.begin(), element.target_refs.end(),
                                      new_id) != element.target_refs.end();
        if (from_parent && to_new) {
            linked = true;
        }
    }
    EXPECT_TRUE(linked) << "the new goal was created but not attached to its parent";
}

TEST(McpProposals, AttributesTheDraftToTheConnectedClient) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("attribution");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);
    const std::string parent_id = FirstClaimId(server);
    ASSERT_FALSE(parent_id.empty());

    const ToolCall created =
        CallTool(server, "create_review_proposal",
                 {{"title", "Attributed change"},
                  {"anchor_element_id", parent_id},
                  {"operations", AddSubGoalOperations(parent_id, "Sub-goal.")}});

    ASSERT_FALSE(created.is_error) << created.payload.dump();
    // Traceable to the client that proposed it, not to an anonymous "AI".
    EXPECT_EQ(created.payload["author"].get<std::string>(), "MCP: TestClient 2.0");
}

TEST(McpProposals, PreviewReportsEffectsAndWritesNothing) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("preview");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);
    const std::string parent_id = FirstClaimId(server);
    ASSERT_FALSE(parent_id.empty());

    const ToolCall preview =
        CallTool(server, "preview_review_proposal",
                 {{"title", "Not saved"},
                  {"anchor_element_id", parent_id},
                  {"operations", AddSubGoalOperations(parent_id, "Sub-goal.")}});

    ASSERT_FALSE(preview.is_error) << preview.payload.dump();
    EXPECT_TRUE(preview.payload["would_apply"].get<bool>());
    EXPECT_FALSE(preview.payload["saved"].get<bool>());

    const ToolCall listed = CallTool(server, "list_review_proposals");
    ASSERT_FALSE(listed.is_error) << listed.payload.dump();
    EXPECT_EQ(listed.payload["count"].get<int>(), 0) << "preview must not write a draft";
}

// Nothing invalid should reach disk for a human to discover later, so create
// runs the same dry run preview does and refuses on the same grounds.
TEST(McpProposals, RefusesAProposalThatCannotApplyAndWritesNothing) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("refuses");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);

    const ToolCall created =
        CallTool(server, "create_review_proposal",
                 {{"title", "Broken"},
                  {"operations", AddSubGoalOperations("no-such-element", "Sub-goal.")}});

    EXPECT_TRUE(created.is_error);
    EXPECT_NE(created.payload.value("error", std::string{}).find("no-such-element"),
              std::string::npos)
        << created.payload.dump();

    const ToolCall listed = CallTool(server, "list_review_proposals");
    ASSERT_FALSE(listed.is_error) << listed.payload.dump();
    EXPECT_EQ(listed.payload["count"].get<int>(), 0);
}

TEST(McpProposals, RejectsAnOperationReferenceThatIsBothIdAndRef) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("ambiguous");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);
    const std::string parent_id = FirstClaimId(server);
    ASSERT_FALSE(parent_id.empty());

    const ToolCall created = CallTool(
        server, "create_review_proposal",
        {{"title", "Ambiguous"},
         {"anchor_element_id", parent_id},
         {"operations",
          nlohmann::json::array(
              {nlohmann::json{{"type", "CreateClaim"}, {"create_ref", "$sub"}, {"text", "x"}},
               nlohmann::json{{"type", "AddSupportedBy"},
                              {"source", {{"id", parent_id}, {"ref", "$sub"}}},
                              {"target", {{"ref", "$sub"}}}}})}});

    EXPECT_TRUE(created.is_error);
    EXPECT_NE(created.payload.value("error", std::string{}).find("exactly one"), std::string::npos)
        << created.payload.dump();
}

TEST(McpProposals, ListsSavedDraftsWithTheirValidity) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("listing");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);
    const std::string parent_id = FirstClaimId(server);
    ASSERT_FALSE(parent_id.empty());

    const ToolCall created =
        CallTool(server, "create_review_proposal",
                 {{"title", "Listed change"},
                  {"anchor_element_id", parent_id},
                  {"operations", AddSubGoalOperations(parent_id, "Sub-goal.")}});
    ASSERT_FALSE(created.is_error) << created.payload.dump();

    const ToolCall listed = CallTool(server, "list_review_proposals");
    ASSERT_FALSE(listed.is_error) << listed.payload.dump();
    ASSERT_EQ(listed.payload["count"].get<int>(), 1);
    EXPECT_EQ(listed.payload["proposals"][0]["title"], "Listed change");
    EXPECT_TRUE(listed.payload["proposals"][0]["valid"].get<bool>());

    const ToolCall fetched = CallTool(
        server, "get_review_proposal", {{"id", created.payload["proposal_id"].get<std::string>()}});
    ASSERT_FALSE(fetched.is_error) << fetched.payload.dump();
    EXPECT_TRUE(fetched.payload["valid"].get<bool>());
    EXPECT_EQ(fetched.payload["proposal"]["title"], "Listed change");
}

// Proposals live in the project directory. A standalone SACM file has none, and
// saying so beats writing the draft somewhere the user will never find it.
TEST(McpProposals, RefusesToProposeAgainstAStandaloneSacmFile) {
    const TempDir             workspace{UniqueTempPath("standalone")};
    mcp::Session::Config      config;
    config.project_path  = std::filesystem::path(AF_REPO_ROOT) / "tests" / "data" /
                          "fixture_roundtrip_core_argument.sacm.xml";
    config.settings_path = WriteConsentingSettings(workspace.path);

    std::string                   error;
    std::unique_ptr<mcp::Session> session = mcp::Session::Open(std::move(config), error);
    ASSERT_NE(session, nullptr) << error;
    ASSERT_FALSE(session->has_project());

    mcp::Server server(*session);
    Initialize(server);

    const ToolCall created =
        CallTool(server, "create_review_proposal",
                 {{"title", "No project"},
                  {"operations", AddSubGoalOperations("cl_top", "Sub-goal.")}});

    EXPECT_TRUE(created.is_error);
    EXPECT_NE(created.payload.value("error", std::string{}).find("project"), std::string::npos)
        << created.payload.dump();
}

// ---------------------------------------------------------------------------
// Proposal visibility
//
// Reported from real use: a proposal created over MCP never appeared in the app.
// It was on disk and valid, but orphaned twice over -- untracked by the project
// manifest, and attached to no review item. The Review panel walks review items
// and renders a proposal only for those carrying a `proposal_id`, so one without
// an item cannot be seen or accepted however well-formed it is.
// ---------------------------------------------------------------------------

// The MCP server writes proposal files and NOTHING else. It briefly also wrote
// the review item and the manifest entry, which made it a second writer of files
// the application saves whole -- so the next save there reverted both and the
// proposal silently vanished. Reported from real use. The application now adopts
// what it finds in reviews/proposals/, and this pins the split.
TEST(McpProposals, TouchesNeitherTheManifestNorTheReviewItems) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("singlewriter");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);
    const std::string parent_id = FirstClaimId(server);
    ASSERT_FALSE(parent_id.empty());

    const std::filesystem::path manifest = fixture->project_root / "af.proj";
    const std::filesystem::path review_items =
        fixture->project_root / "reviews" / "review-items.af.json";

    const auto read = [](const std::filesystem::path& path) -> std::string {
        std::ifstream      in(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    };
    const std::string manifest_before = read(manifest);
    const std::string review_before   = read(review_items);

    const ToolCall created =
        CallTool(server, "create_review_proposal",
                 {{"title", "Single writer"},
                  {"anchor_element_id", parent_id},
                  {"operations", AddSubGoalOperations(parent_id, "Sub-goal.")}});
    ASSERT_FALSE(created.is_error) << created.payload.dump();

    EXPECT_EQ(read(manifest), manifest_before)
        << "the MCP server rewrote af.proj; the application owns that file";
    EXPECT_EQ(read(review_items), review_before)
        << "the MCP server rewrote review-items.af.json; the application owns that file";

    // The one thing it does own.
    const std::filesystem::path written =
        fixture->project_root / created.payload["path"].get<std::string>();
    EXPECT_TRUE(std::filesystem::exists(written)) << written.string();
}

// ---------------------------------------------------------------------------
// Multi-argument projects
//
// Also reported: a second argument file in the project was unreachable. The
// session opens the first on load and nothing could see or select the others.
// ---------------------------------------------------------------------------

namespace {

// Adds a second argument to the fixture's project and reopens the session so it
// sees the manifest that file was added to.
std::unique_ptr<mcp::Session> ReopenWithSecondArgument(Fixture& fixture) {
    core::AppState builder;
    if (!builder.open_project(fixture.project_root.string())) {
        ADD_FAILURE() << "could not reopen project: " << builder.status_message;
        return nullptr;
    }
    if (!builder.create_project_sacm_file("main2.sacm")) {
        ADD_FAILURE() << "could not add a second argument: " << builder.status_message;
        return nullptr;
    }

    mcp::Session::Config config;
    config.project_path  = fixture.project_root;
    config.settings_path = WriteConsentingSettings(fixture.workspace.path);
    std::string                   error;
    std::unique_ptr<mcp::Session> session = mcp::Session::Open(std::move(config), error);
    if (session == nullptr) {
        ADD_FAILURE() << "could not reopen MCP session: " << error;
    }
    return session;
}

std::string UnloadedCaseFile(const ToolCall& listed) {
    for (const nlohmann::json& file : listed.payload["case_files"]) {
        if (!file["loaded"].get<bool>()) {
            return file["path"].get<std::string>();
        }
    }
    return {};
}

} // namespace

TEST(McpCaseFiles, ListsEveryArgumentAndMarksTheLoadedOne) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("listfiles");
    ASSERT_NE(fixture, nullptr);
    std::unique_ptr<mcp::Session> session = ReopenWithSecondArgument(*fixture);
    ASSERT_NE(session, nullptr);

    mcp::Server server(*session);
    Initialize(server);

    const ToolCall listed = CallTool(server, "list_case_files");

    ASSERT_FALSE(listed.is_error) << listed.payload.dump();
    ASSERT_EQ(listed.payload["case_files"].size(), 2u) << listed.payload.dump();

    int loaded_count = 0;
    for (const nlohmann::json& file : listed.payload["case_files"]) {
        if (file["loaded"].get<bool>()) {
            ++loaded_count;
        }
    }
    EXPECT_EQ(loaded_count, 1) << "exactly one argument should be reported as loaded";
}

TEST(McpCaseFiles, OpensASecondArgumentAndKeepsReadingIt) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("switchfiles");
    ASSERT_NE(fixture, nullptr);
    std::unique_ptr<mcp::Session> session = ReopenWithSecondArgument(*fixture);
    ASSERT_NE(session, nullptr);

    mcp::Server server(*session);
    Initialize(server);

    const ToolCall listed = CallTool(server, "list_case_files");
    ASSERT_FALSE(listed.is_error) << listed.payload.dump();
    const std::string unloaded = UnloadedCaseFile(listed);
    ASSERT_FALSE(unloaded.empty()) << "expected a second, unloaded argument";

    const ToolCall opened = CallTool(server, "open_case_file", {{"path", unloaded}});
    ASSERT_FALSE(opened.is_error) << opened.payload.dump();
    EXPECT_NE(opened.payload["loaded_file"].get<std::string>().find(unloaded), std::string::npos)
        << opened.payload.dump();

    // The switch must stick for later reads, not just colour the return value.
    const ToolCall after = CallTool(server, "get_case_overview");
    ASSERT_FALSE(after.is_error) << after.payload.dump();
    EXPECT_NE(after.payload["loaded_file"].get<std::string>().find(unloaded), std::string::npos);
}

// docs/features/mcp-server.md states the server never writes SACM XML -- that is
// what keeps the safety case single-writer and lets proposals be the only route
// by which an agent changes anything. Switching arguments and saving a proposal
// are the two operations most likely to breach it by accident, so this pins the
// bytes rather than trusting the claim.
TEST(McpCaseFiles, NeitherSwitchingNorProposingRewritesAnySacmFile) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("nosacmwrite");
    ASSERT_NE(fixture, nullptr);
    std::unique_ptr<mcp::Session> session = ReopenWithSecondArgument(*fixture);
    ASSERT_NE(session, nullptr);

    const std::filesystem::path arguments = fixture->project_root / "arguments";
    std::map<std::string, std::string> before;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(arguments)) {
        if (entry.is_regular_file()) {
            std::ifstream      in(entry.path(), std::ios::binary);
            std::ostringstream buffer;
            buffer << in.rdbuf();
            before[entry.path().filename().string()] = buffer.str();
        }
    }
    ASSERT_FALSE(before.empty());

    mcp::Server server(*session);
    Initialize(server);

    const ToolCall listed = CallTool(server, "list_case_files");
    ASSERT_FALSE(listed.is_error) << listed.payload.dump();
    const std::string unloaded = UnloadedCaseFile(listed);
    ASSERT_FALSE(unloaded.empty());
    ASSERT_FALSE(CallTool(server, "open_case_file", {{"path", unloaded}}).is_error);

    const std::string claim = FirstClaimId(server);
    if (!claim.empty()) {
        CallTool(server, "create_review_proposal",
                 {{"title", "No SACM write"},
                  {"anchor_element_id", claim},
                  {"operations", AddSubGoalOperations(claim, "Sub-goal.")}});
    }

    for (const std::pair<const std::string, std::string>& entry : before) {
        std::ifstream      in(arguments / entry.first, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        EXPECT_EQ(buffer.str(), entry.second)
            << entry.first << " was rewritten; the MCP server must never write SACM";
    }
}

TEST(McpCaseFiles, ReportsAnUnknownPathRatherThanSilentlyKeepingTheOldCase) {
    std::unique_ptr<Fixture> fixture = MakeProjectFixture("badswitch");
    ASSERT_NE(fixture, nullptr);
    mcp::Server server(*fixture->session);
    Initialize(server);

    const ToolCall opened = CallTool(server, "open_case_file", {{"path", "arguments/nope.sacm"}});

    EXPECT_TRUE(opened.is_error);
    EXPECT_NE(opened.payload.value("error", std::string{}).find("nope.sacm"), std::string::npos);
}
