#include "core/changesets/change_set_store.h"

#include "agent/operations.h"
#include "core/audit/audit_store.h"
#include "core/audit/event_store.h"
#include "core/commands/command_bus.h"
#include "core/commands/proposal_commands.h"
#include "core/drafts/draft_promotion_service.h"
#include "core/library_package_projection.h"
#include "core/terminology_package_service.h"
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

ProjectFixture MakeFixture(const std::string& tag, std::string_view sacm_content = kSampleSacm) {
    ProjectFixture f;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("af_changeset_accept_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, sacm_content);

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

    std::expected<parser::AssuranceCase, std::string> parsed = parser::parse_sacm_xml_string(std::string(sacm_content));
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
                                              {"field", "content"},
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

// ---------------------------------------------------------------------------
// Terminology through the same pipeline. A term staged over MCP is promoted by
// the same audited command as an argument edit, lands in the library document,
// and survives to the saved XML -- including bringing the case's first
// TerminologyPackage into being when there is none to put it in.
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kTerminologySacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="AC1">
  <name content="Sample" />
  <argumentPackage xmi:id="AP1">
    <name content="Args" />
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1">
      <name content="Top goal" />
      <description xmi:id="d1">
        <content>
          <value lang="en" content="The system is safe." />
        </content>
      </description>
    </argumentElement>
  </argumentPackage>
  <terminologyPackage xmi:id="TP1" gid="gid-TP1">
    <name content="Terminology" />
    <terminologyElement xsi:type="sacm:Term" xmi:id="T1" gid="gid-T1" value="ALARP">
      <name content="ALARP" />
      <description xmi:id="d2">
        <content>
          <value lang="en" content="As low as reasonably practicable." />
        </content>
      </description>
    </terminologyElement>
    <terminologyElement xsi:type="sacm:Term" xmi:id="T2" gid="gid-T2" value="SFAIRP">
      <name content="SFAIRP" />
      <description xmi:id="d3">
        <content>
          <value lang="en" content="So far as is reasonably practicable." />
        </content>
      </description>
    </terminologyElement>
  </terminologyPackage>
</sacm:AssuranceCasePackage>
)";

const parser::SacmElement* FindTermByValue(const parser::AssuranceCase& model, const std::string& value) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.type == "term" && element.content == value) {
            return &element;
        }
    }
    return nullptr;
}

std::vector<core::reviews::PatchOperation> ParseOperations(const nlohmann::json& operations) {
    std::vector<core::reviews::PatchOperation> parsed;
    std::string error;
    EXPECT_TRUE(agent::ParsePatchOperations(operations, parsed, error)) << error;
    return parsed;
}

} // namespace

TEST(ChangeSetAcceptance, AcceptsATermCreationCreatingTheTerminologyPackage) {
    ProjectFixture f = MakeFixture("term_create");

    core::changesets::ChangeSetStore store;
    const std::string id = store.Begin(1, "Define the hazard vocabulary", "", "", "claude-ai");
    std::string error;
    const nlohmann::json operations = nlohmann::json::array({nlohmann::json{
        {"type", "CreateTerm"},
        {"create_ref", "$hazard"},
        {"text", "hazard"},
        {"new_value", "A system state that, together with environmental conditions, could lead to harm."}}});
    ASSERT_TRUE(store.Stage(id, ParseOperations(operations), f.model, error)) << error;

    const core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(store.Find(id)->proposal, f.model);
    ASSERT_EQ(validity.validity, core::reviews::ProposalValidity::Valid) << validity.reason;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);

    // What the acceptance preflight predicts and what the real apply produces
    // must be the same document -- the same convergence the promotion pipeline
    // verifies by hash before consuming a draft.
    parser::AssuranceCase preflight_model;
    ASSERT_TRUE(core::commands::PreflightProposalAgainstLibrary(
        *loaded.document, store.Find(id)->proposal, {}, preflight_model, error))
        << error;

    std::unique_ptr<core::commands::CommandBus> bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_NE(bus, nullptr) << error;
    core::commands::ApplyProposalCommand command(store.Find(id)->proposal);
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "MCP: claude-ai");
    ASSERT_TRUE(result.success) << result.error;

    const parser::AssuranceCase applied = sacm_adapter::project_case(*loaded.document);
    const parser::SacmElement* term = FindTermByValue(applied, "hazard");
    ASSERT_NE(term, nullptr) << "the created term must survive to the library projection";
    EXPECT_EQ(term->description, "A system state that, together with environmental conditions, could lead to harm.");
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(applied),
              core::reviews::ComputeModelSemanticHash(preflight_model));

    // The sample file has no terminologyPackage, so accepting the first term
    // must create one rather than refuse -- otherwise a case with no glossary
    // could never grow one over MCP.
    const std::vector<sacm::TerminologyPackage> packages = sacm_adapter::project_terminology_packages(*loaded.document);
    ASSERT_EQ(packages.size(), 1u);
    ASSERT_EQ(packages.front().terms.size(), 1u);
    EXPECT_EQ(packages.front().terms.front().value, "hazard");
    EXPECT_EQ(packages.front().terms.front().id, term->id);
}

TEST(ChangeSetAcceptance, AcceptsTermEditsAgainstAnExistingGlossary) {
    ProjectFixture f = MakeFixture("term_edit", kTerminologySacm);

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);
    // The model the change set is staged against comes from the library
    // projection, the way the application's does -- the legacy parser never
    // sees terms, so staging against its output would refuse every term edit.
    f.model = sacm_adapter::project_case(*loaded.document);

    core::changesets::ChangeSetStore store;
    const std::string id = store.Begin(1, "Tighten the ALARP definition", "", "", "claude-ai");
    std::string error;
    const nlohmann::json operations = nlohmann::json::array(
        {nlohmann::json{{"type", "UpdateTerm"},
                        {"element", {{"id", "T1"}}},
                        {"field", "definition"},
                        {"new_value", "Risk reduced as low as reasonably practicable, per the safety case scope."}},
         nlohmann::json{
             {"type", "UpdateTerm"}, {"element", {{"id", "T1"}}}, {"field", "value"}, {"new_value", "ALARP principle"}},
         nlohmann::json{{"type", "RemoveTerm"}, {"element", {{"id", "T2"}}}}});
    ASSERT_TRUE(store.Stage(id, ParseOperations(operations), f.model, error)) << error;

    const core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(store.Find(id)->proposal, f.model);
    ASSERT_EQ(validity.validity, core::reviews::ProposalValidity::Valid) << validity.reason;

    std::unique_ptr<core::commands::CommandBus> bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_NE(bus, nullptr) << error;
    core::commands::ApplyProposalCommand command(store.Find(id)->proposal);
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "MCP: claude-ai");
    ASSERT_TRUE(result.success) << result.error;

    const std::vector<sacm::TerminologyPackage> packages = sacm_adapter::project_terminology_packages(*loaded.document);
    ASSERT_EQ(packages.size(), 1u);
    ASSERT_EQ(packages.front().terms.size(), 1u) << "T2 must be removed";
    EXPECT_EQ(packages.front().terms.front().id, "T1");
    EXPECT_EQ(packages.front().terms.front().value, "ALARP principle");
    EXPECT_EQ(packages.front().terms.front().description,
              "Risk reduced as low as reasonably practicable, per the safety case scope.");
}

// The reported case: a glossary written over MCP left every term flagged by the
// terminology check -- no category, no external reference -- and the agent had
// no operation that could answer either finding. This drives the fix through
// the real validator: the findings exist first, the staged operations clear
// them, and the classification survives into the saved file.
TEST(ChangeSetAcceptance, ClassifyingAndCitingTermsClearsTheTerminologyFindings) {
    ProjectFixture f = MakeFixture("term_findings", kTerminologySacm);

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);
    f.model = sacm_adapter::project_case(*loaded.document);

    const auto findings_for = [](const sacm_adapter::LibraryDocument& document) {
        const sacm::AssuranceCasePackage package = core::project_library_package_with_tags(document);
        std::map<core::TerminologyTermIssueKind, int> counts;
        for (const sacm::TerminologyPackage& terminology : package.terminologyPackages) {
            for (const core::TerminologyTermIssue& issue : core::ValidateTerminologyTerms(terminology))
                ++counts[issue.kind];
        }
        return counts;
    };

    // Both fixture terms are uncategorized and uncited, which is exactly what
    // the user saw after a glossary was written over MCP.
    const std::map<core::TerminologyTermIssueKind, int> before = findings_for(*loaded.document);
    EXPECT_EQ(before.at(core::TerminologyTermIssueKind::MissingCategory), 2);
    EXPECT_EQ(before.at(core::TerminologyTermIssueKind::MissingExternalReference), 2);

    core::changesets::ChangeSetStore store;
    const std::string id = store.Begin(1, "Classify and cite the glossary", "", "", "claude-ai");
    std::string error;
    nlohmann::json operations = nlohmann::json::array({nlohmann::json{{"type", "CreateCategory"},
                                                                      {"create_ref", "$regulatory"},
                                                                      {"text", "Regulatory terms"},
                                                                      {"new_value", "Terms drawn from regulation."}}});
    ASSERT_TRUE(store.Stage(id, ParseOperations(operations), f.model, error)) << error;

    // The id the category will get, reported by staging exactly as an agent
    // reads it out of the stage_operations result.
    const core::changesets::ChangeSetDiff staged = core::changesets::ComputeChangeSetDiff(*store.Find(id), f.model);
    ASSERT_TRUE(staged.success) << staged.error;
    const std::string category_id = staged.generated_ids.at("$regulatory");

    nlohmann::json classify = nlohmann::json::array();
    for (const std::string& term_id : {std::string("T1"), std::string("T2")}) {
        classify.push_back(nlohmann::json{
            {"type", "UpdateTerm"}, {"element", {{"id", term_id}}}, {"field", "category"}, {"new_value", category_id}});
        classify.push_back(nlohmann::json{{"type", "UpdateTerm"},
                                          {"element", {{"id", term_id}}},
                                          {"field", "external_reference"},
                                          {"new_value", "HSE R2P2, 2001"}});
    }
    ASSERT_TRUE(store.Stage(id, ParseOperations(classify), f.model, error)) << error;

    const core::reviews::ProposalValidityResult validity =
        core::reviews::EvaluateReviewProposalValidity(store.Find(id)->proposal, f.model);
    ASSERT_EQ(validity.validity, core::reviews::ProposalValidity::Valid) << validity.reason;

    std::unique_ptr<core::commands::CommandBus> bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_NE(bus, nullptr) << error;
    core::commands::ApplyProposalCommand command(store.Find(id)->proposal);
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "MCP: claude-ai");
    ASSERT_TRUE(result.success) << result.error;

    // The findings the user was left with are gone, judged by the same
    // validator that raised them rather than by inspecting fields.
    sacm_adapter::LoadOutcome reloaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(reloaded.ok);
    const std::map<core::TerminologyTermIssueKind, int> after = findings_for(*reloaded.document);
    EXPECT_EQ(after.count(core::TerminologyTermIssueKind::MissingCategory), 0u);
    EXPECT_EQ(after.count(core::TerminologyTermIssueKind::MissingExternalReference), 0u);

    const std::vector<sacm::TerminologyPackage> packages =
        sacm_adapter::project_terminology_packages(*reloaded.document);
    ASSERT_EQ(packages.size(), 1u);
    ASSERT_EQ(packages.front().categories.size(), 1u);
    EXPECT_EQ(packages.front().categories.front().name, "Regulatory terms");
    for (const sacm::Term& term : packages.front().terms) {
        ASSERT_EQ(term.category_refs.size(), 1u) << term.id;
        EXPECT_EQ(term.category_refs.front(), category_id);
        EXPECT_EQ(term.externalReference, "HSE R2P2, 2001");
    }
}

// Accepting one group must leave the rest of the draft applying.
//
// Promotion re-anchors the surviving groups to the newly accepted argument. Two
// models could serve: the one the patch service PREDICTED, and the one the
// library PRODUCED. They agree for an argument edit, which is why re-anchoring
// to the prediction stood for so long -- and they disagree for terminology,
// because the seams stamp a legacy gid a flat patch cannot know. Anchored to the
// prediction, the next open compared a baseline describing an argument that
// never existed, declared the draft stale, and applied none of it: terminology
// work that had been accepted appeared to stop taking effect.
TEST(ChangeSetAcceptance, AcceptingATerminologyGroupLeavesTheRestOfTheDraftApplying) {
    ProjectFixture f = MakeFixture("promoted_model_parity");

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(f.project.rootPath);
    std::string error;
    ASSERT_TRUE(drafts.Open(f.sacm_abs, f.model, error)) << error;

    core::drafts::DraftGroupRequest request;
    request.title = "Define a term";
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = "claude-ai";
    const std::string group = drafts.BeginGroup(request, f.model, error);
    ASSERT_FALSE(group.empty()) << error;

    const nlohmann::json operations =
        nlohmann::json::array({nlohmann::json{{"type", "CreateTerm"},
                                              {"create_ref", "$hazard"},
                                              {"text", "hazard"},
                                              {"new_value", "A system state that could lead to harm."}}});
    ASSERT_TRUE(drafts.StageOperations(group, ParseOperations(operations), f.model, error)) << error;
    ASSERT_TRUE(drafts.MarkGroupReady(group, error)) << error;

    // A second group, left unaccepted. This is the ordinary case -- an agent
    // stages more than one contribution -- and it is what makes the divergence
    // visible: accepting one group must not strand the others.
    core::drafts::DraftGroupRequest later_request;
    later_request.title = "Develop the top goal";
    later_request.source = core::drafts::DraftSource::Mcp;
    later_request.source_label = "claude-ai";
    const std::string later = drafts.BeginGroup(later_request, f.model, error);
    ASSERT_FALSE(later.empty()) << error;
    const nlohmann::json later_operations = nlohmann::json::array(
        {nlohmann::json{{"type", "CreateClaim"}, {"create_ref", "$sub"}, {"text", "Maintenance is adequate."}},
         nlohmann::json{{"type", "AddSupportedBy"}, {"source", {{"ref", "$sub"}}}, {"target", {{"id", "G1"}}}}});
    ASSERT_TRUE(drafts.StageOperations(later, ParseOperations(later_operations), f.model, error)) << error;

    const core::drafts::DraftPromotionPlan plan = core::drafts::PlanDraftPromotion(
        *drafts.workspace(), f.model, {group}, "MCP: claude-ai", drafts.authoritative_identities());
    ASSERT_TRUE(plan.ok) << plan.error;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);
    std::unique_ptr<core::commands::CommandBus> bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_NE(bus, nullptr) << error;
    core::commands::ApplyProposalCommand command(plan.compiled.proposal, plan.compiled.identities);
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    ASSERT_TRUE(bus->Execute(command, ctx, "MCP: claude-ai").success);

    const parser::AssuranceCase authoritative = sacm_adapter::project_case(*loaded.document);

    // The hazard this exists for. The terminology seams stamp a legacy gid on a
    // created term and the flat patch that predicted the promotion cannot know
    // one, so the two models genuinely disagree. If this ever stops holding --
    // the prediction became exact -- the guard below is moot and can go; until
    // then it is why the runtime must not re-anchor to the prediction.
    ASSERT_NE(core::reviews::ComputeModelSemanticHash(plan.promoted_model),
              core::reviews::ComputeModelSemanticHash(authoritative))
        << "expected the predicted and produced models to differ for a created term";

    // Re-anchored the way `AppRuntime::PromoteDraftGroups` does it: to the
    // argument the library produced, not the one the patch predicted.
    ASSERT_TRUE(drafts.RemovePromotedGroups({group}, authoritative, error)) << error;

    core::drafts::DraftWorkspaceStore reopened;
    reopened.SetProjectRoot(f.project.rootPath);
    ASSERT_TRUE(reopened.Open(f.sacm_abs, authoritative, error)) << error;
    ASSERT_TRUE(reopened.has_workspace()) << "the unaccepted group must survive";
    EXPECT_EQ(reopened.workspace()->state, core::drafts::DraftWorkspaceState::Active)
        << "accepting a terminology group must not strand the rest of the draft";
    ASSERT_NE(reopened.workspace()->FindGroup(later), nullptr);

    // And it still applies: a surviving group that materializes is the whole
    // point of the baseline being right.
    const core::drafts::DraftMaterializationResult& materialized = reopened.Materialize(authoritative, 1);
    EXPECT_TRUE(materialized.success) << materialized.error;
}

// The application's Accept All, end to end, for a glossary staged over MCP into
// a case that has never had one: draft groups -> promotion plan with compiled,
// pinned identities -> preflight -> the audited command -> the saved file. This
// is the path the change-set tests above DO NOT take -- they build the proposal
// directly -- and it is the path a real MCP contribution is accepted through.
TEST(ChangeSetAcceptance, AcceptAllPromotesADraftedGlossaryIntoTheSavedFile) {
    ProjectFixture f = MakeFixture("term_draft_accept_all");

    core::drafts::DraftWorkspaceStore drafts;
    drafts.SetProjectRoot(f.project.rootPath);
    std::string error;
    ASSERT_TRUE(drafts.Open(f.sacm_abs, f.model, error)) << error;

    core::drafts::DraftGroupRequest request;
    request.title = "Bound the safety vocabulary";
    request.source = core::drafts::DraftSource::Mcp;
    request.source_label = "claude-ai";
    request.source_session_id = "session-terms";
    const std::string group = drafts.BeginGroup(request, f.model, error);
    ASSERT_FALSE(group.empty()) << error;

    nlohmann::json operations = nlohmann::json::array();
    const std::vector<std::pair<std::string, std::string>> glossary = {
        {"hazard", "A system state that, with environmental conditions, could lead to harm."},
        {"ALARP", "Risk reduced as low as reasonably practicable."},
        {"safe state", "A state with an acceptable level of risk."},
    };
    for (std::size_t index = 0; index < glossary.size(); ++index) {
        operations.push_back(nlohmann::json{{"type", "CreateTerm"},
                                            {"create_ref", "$term" + std::to_string(index)},
                                            {"text", glossary[index].first},
                                            {"new_value", glossary[index].second}});
    }
    ASSERT_TRUE(drafts.StageOperations(group, ParseOperations(operations), f.model, error)) << error;
    ASSERT_TRUE(drafts.MarkGroupReady(group, error)) << error;

    // The plan the application computes before touching anything.
    const core::drafts::DraftPromotionPlan plan = core::drafts::PlanDraftPromotion(
        *drafts.workspace(), f.model, {group}, "MCP: claude-ai", drafts.authoritative_identities());
    ASSERT_TRUE(plan.ok) << plan.error;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(loaded.ok);

    // The preflight the application requires before dispatch, and whose result
    // it hash-compares against the real apply afterwards.
    parser::AssuranceCase preflight_model;
    ASSERT_TRUE(core::commands::PreflightProposalAgainstLibrary(
        *loaded.document, plan.compiled.proposal, plan.compiled.identities, preflight_model, error))
        << error;

    std::unique_ptr<core::commands::CommandBus> bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_NE(bus, nullptr) << error;
    core::commands::ApplyProposalCommand command(plan.compiled.proposal, plan.compiled.identities);
    core::commands::CommandContext ctx{f.model, f.package, loaded.document.get()};
    const core::commands::CommandResult result = bus->Execute(command, ctx, "MCP: claude-ai");
    ASSERT_TRUE(result.success) << result.error;

    // The post-apply verification the application performs before consuming the
    // draft: a mismatch there keeps the draft and reports failure to the user.
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(sacm_adapter::project_case(*loaded.document)),
              core::reviews::ComputeModelSemanticHash(preflight_model));

    // What the user reopens: the saved file. The terminology panel reads the
    // package projected from it, so the terms must be there, not merely in the
    // in-memory document the command mutated.
    sacm_adapter::LoadOutcome reloaded = sacm_adapter::load_document(f.sacm_abs);
    ASSERT_TRUE(reloaded.ok);
    const std::vector<sacm::TerminologyPackage> packages =
        sacm_adapter::project_terminology_packages(*reloaded.document);
    ASSERT_EQ(packages.size(), 1u) << "accepting the first terms must create the terminology package";
    ASSERT_EQ(packages.front().terms.size(), glossary.size());
    for (const auto& [value, definition] : glossary) {
        const auto found = std::find_if(packages.front().terms.begin(),
                                        packages.front().terms.end(),
                                        [&](const sacm::Term& term) { return term.value == value; });
        ASSERT_NE(found, packages.front().terms.end()) << value << " missing from the saved glossary";
        EXPECT_EQ(found->description, definition);
    }
}
