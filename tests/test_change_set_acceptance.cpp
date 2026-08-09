#include "core/changesets/change_set_store.h"

#include "agent/operations.h"
#include "core/audit/audit_store.h"
#include "core/audit/event_store.h"
#include "core/commands/command_bus.h"
#include "core/commands/proposal_commands.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_parser.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

// Accepting an agent's change set is an ordinary audited edit.
//
// That is the whole claim of the design: an agent's change goes through the
// same command, the same validation, the same audit log and the same undo as
// one made with the mouse, so it cannot do anything the application could not
// do itself. These tests exercise it through a real `CommandBus` against a real
// project on disk, rather than trusting the wiring.

namespace {

constexpr const char* kSampleSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct ProjectFixture {
    core::AssuranceProject project;
    std::filesystem::path sacm_abs;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
};

ProjectFixture MakeFixture(const std::string& tag) {
    ProjectFixture f;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("af_changeset_accept_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);

    f.project.id = "p";
    f.project.name = "Project";
    f.project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    f.project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(f.project, sacm_rel, ensure, error)) << error;

    f.sacm_abs = f.project.rootPath / sacm_rel;
    std::expected<sacm::AssuranceCasePackage, std::string> pkg = sacm::parse_sacm(f.sacm_abs.string());
    EXPECT_TRUE(pkg.has_value()) << (pkg.has_value() ? "" : pkg.error());
    f.package = std::move(pkg.value());

    std::expected<parser::AssuranceCase, std::string> parsed = parser::parse_sacm_xml_string(kSampleSacm);
    EXPECT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error());
    f.model = std::move(parsed.value());
    return f;
}

std::vector<core::reviews::PatchOperation> AddSubGoalUnder(const std::string& parent_id, const std::string& text) {
    const nlohmann::json operations = nlohmann::json::array(
        {nlohmann::json{{"type", "CreateClaim"}, {"create_ref", "$sub"}, {"text", text}},
         nlohmann::json{{"type", "AddSupportedBy"}, {"source", {{"ref", "$sub"}}}, {"target", {{"id", parent_id}}}}});

    std::vector<core::reviews::PatchOperation> parsed;
    std::string error;
    EXPECT_TRUE(agent::ParsePatchOperations(operations, parsed, error)) << error;
    return parsed;
}

std::size_t ClaimCount(const parser::AssuranceCase& model) {
    std::size_t count = 0;
    for (const parser::SacmElement& element : model.elements) {
        if (element.type == "claim") {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST(ChangeSetAcceptance, AppliesThroughTheAuditedCommandBus) {
    ProjectFixture f = MakeFixture("audited");

    core::changesets::ChangeSetStore store;
    const std::string id = store.Begin(1, "Add a maintenance sub-goal", "", "", "claude-ai 0.1.0");
    std::string error;
    ASSERT_TRUE(store.Stage(id, AddSubGoalUnder("G1", "Maintenance is adequate"), f.model, error)) << error;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);

    std::unique_ptr<core::commands::CommandBus> bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_NE(bus, nullptr) << error;
    const std::uint64_t before = bus->Store().LatestTransactionSequence();

    // The same command an accepted proposal has always used. No agent-specific
    // command type exists, which is what guarantees an agent cannot make a
    // change the application could not make itself.
    core::commands::ApplyProposalCommand command(store.Find(id)->proposal);
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "MCP: claude-ai 0.1.0");
    ASSERT_TRUE(result.success) << result.error;

    // One transaction, in the append-only log, like any other edit.
    EXPECT_EQ(bus->Store().LatestTransactionSequence(), before + 1);
    ASSERT_FALSE(bus->Store().Transactions().empty());
    const core::audit::AuditTransaction& transaction = bus->Store().Transactions().back();
    EXPECT_EQ(transaction.command_name, "ApplyProposal");
    // Attributed to the client that proposed it, not to "the AI".
    //
    // Note what this does and does not prove: the author is passed to `Execute`
    // here, so this pins the bus recording what it is given. It says nothing
    // about the application passing it. It did not -- `DispatchAuditedCommand`
    // sent an empty author and the log read `system` -- and this test was green
    // throughout. Found by accepting a change set in the running application and
    // reading the transaction log, which is the only place that wiring shows.
    EXPECT_EQ(transaction.author, "MCP: claude-ai 0.1.0");
    EXPECT_EQ(store.Find(id)->proposal.author_name, "MCP: claude-ai 0.1.0")
        << "the change set must carry the author the application hands to the bus";

    // `ApplyProposalCommand` runs the patch through the library bridge on a
    // scratch projection, and the application re-derives its render model from
    // the library afterwards. Asserting against that projection checks what the
    // user would actually see, rather than a POD copy the command never touched.
    const parser::AssuranceCase applied = sacm_adapter::project_case(*loaded.document);
    EXPECT_EQ(ClaimCount(applied), 2u);
}

// What the user approved on the canvas is what lands. If the preview and the
// apply could disagree, the canvas would be showing something other than the
// change being accepted.
TEST(ChangeSetAcceptance, AppliesExactlyWhatThePreviewShowed) {
    ProjectFixture f = MakeFixture("matches");

    core::changesets::ChangeSetStore store;
    const std::string id = store.Begin(1, "Add a sub-goal", "", "", "claude-ai");
    std::string error;
    ASSERT_TRUE(store.Stage(id, AddSubGoalUnder("G1", "Thermal runaway is mitigated"), f.model, error)) << error;

    const core::changesets::ChangeSetDiff preview = core::changesets::ComputeChangeSetDiff(*store.Find(id), f.model);
    ASSERT_TRUE(preview.success) << preview.error;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);
    std::unique_ptr<core::commands::CommandBus> bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_NE(bus, nullptr) << error;

    core::commands::ApplyProposalCommand command(store.Find(id)->proposal);
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    ASSERT_TRUE(bus->Execute(command, ctx, "MCP: claude-ai").success);

    const parser::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    for (const std::pair<const std::string, core::changesets::ElementChange>& entry : preview.status_by_id) {
        if (entry.second != core::changesets::ElementChange::Added) {
            continue;
        }
        const parser::SacmElement* previewed = parser::FindElementById(preview.preview_model, entry.first);
        const parser::SacmElement* applied = parser::FindElementById(after, entry.first);
        ASSERT_NE(applied, nullptr) << "preview added " << entry.first << " but apply did not";
        EXPECT_EQ(applied->content, previewed->content);
        EXPECT_EQ(applied->name, previewed->name);
    }
    EXPECT_EQ(ClaimCount(after), ClaimCount(preview.preview_model));
}

// Restructuring a real argument is not two operations. The change sets reported
// from live use carried eighty, and Accept reported nothing at all -- so the
// size is what is under test here, not the mechanism. The two-operation cases
// above pass either way and did not catch it.
TEST(ChangeSetAcceptance, AcceptsALargeMultiOperationChangeSet) {
    ProjectFixture f = MakeFixture("large");

    core::changesets::ChangeSetStore store;
    const std::string id = store.Begin(1, "Restructure into hazard categories", "", "", "claude-ai");

    nlohmann::json operations = nlohmann::json::array();
    for (int index = 0; index < 20; ++index) {
        const std::string suffix = std::to_string(index);
        const std::string strategy = "$strategy" + suffix;
        const std::string sub_goal = "$goal" + suffix;
        operations.push_back(nlohmann::json{
            {"type", "CreateStrategy"}, {"create_ref", strategy}, {"text", "Argue over hazard " + suffix}});
        operations.push_back(
            nlohmann::json{{"type", "AddSupportedBy"}, {"source", {{"ref", strategy}}}, {"target", {{"id", "G1"}}}});
        operations.push_back(nlohmann::json{
            {"type", "CreateClaim"}, {"create_ref", sub_goal}, {"text", "Hazard " + suffix + " is mitigated"}});
        operations.push_back(nlohmann::json{
            {"type", "AddSupportedBy"}, {"source", {{"ref", sub_goal}}}, {"target", {{"ref", strategy}}}});
    }
    ASSERT_EQ(operations.size(), 80u);

    std::vector<core::reviews::PatchOperation> parsed;
    std::string error;
    ASSERT_TRUE(agent::ParsePatchOperations(operations, parsed, error)) << error;
    ASSERT_TRUE(store.Stage(id, parsed, f.model, error)) << error;

    // The gate acceptance runs before it dispatches. A refusal here and a
    // refusal in the command are indistinguishable to a user watching nothing
    // happen, so they are asserted apart.
    const core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(store.Find(id)->proposal, f.model);
    ASSERT_EQ(validity.validity, core::reviews::ProposalValidity::Valid) << validity.reason;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);
    std::unique_ptr<core::commands::CommandBus> bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_NE(bus, nullptr) << error;

    core::commands::ApplyProposalCommand command(store.Find(id)->proposal);
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "MCP: claude-ai");
    ASSERT_TRUE(result.success) << result.error;

    const parser::AssuranceCase applied = sacm_adapter::project_case(*loaded.document);
    EXPECT_EQ(ClaimCount(applied), 21u);
}

// The user may edit the argument while reading a proposal. Acceptance re-checks
// staleness at that moment rather than trusting the check made when the agent
// staged it.
TEST(ChangeSetAcceptance, RefusesAChangeSetTheArgumentHasMovedUnder) {
    ProjectFixture f = MakeFixture("stale");

    core::changesets::ChangeSetStore store;
    const std::string id = store.Begin(1, "Reword the top goal", "", "", "claude-ai");

    const nlohmann::json operations =
        nlohmann::json::array({nlohmann::json{{"type", "UpdateElementText"},
                                              {"element", {{"id", "G1"}}},
                                              {"field", "description"},
                                              {"old_value", "The system is safe."},
                                              {"new_value", "The system is acceptably safe."}}});
    std::vector<core::reviews::PatchOperation> parsed;
    std::string error;
    ASSERT_TRUE(agent::ParsePatchOperations(operations, parsed, error)) << error;
    ASSERT_TRUE(store.Stage(id, parsed, f.model, error)) << error;

    // The user edits the same element while the proposal waits for review.
    parser::SacmElement* top_goal = parser::FindElementById(f.model, "G1");
    ASSERT_NE(top_goal, nullptr);
    top_goal->description = "The system is safe enough for release.";

    const core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(store.Find(id)->proposal, f.model);

    EXPECT_NE(validity.validity, core::reviews::ProposalValidity::Valid)
        << "a patch built against an older argument must not apply silently";
    EXPECT_FALSE(validity.reason.empty());
}
