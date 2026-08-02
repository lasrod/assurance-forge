// Phase 2 slice 2b-1: the LIVE edit flip. The core GSN element commands
// (CreateTopGoal, CreateChildElement, CreateChallenge, RemoveElement) now mutate
// the SACM library FIRST when a library document is present, and the command bus
// REBUILDS the legacy views (`loaded_case` / `sacm_package`) from it.
//
// Two things have to hold for that flip to be safe, and both are asserted here:
//
//  1. Byte-compatibility. The same command sequence run library-primary and run
//     on the legacy mutators must produce the SAME `core::library_canonical_hash`
//     -- the quantity the audit manifest caches and the verifier compares -- and
//     the same generated ids, so the audit-event payloads and the on-disk ids are
//     unchanged.
//
//  2. The rebuilt views are the views the UI expects. Rebuilding from the library
//     must apply the same render passes `AppState::load_file` applies, or a bare
//     strategy would float with no placement and terminology would render as
//     drawn context nodes instead of inline chips.
//
// Each test drives the REAL command bus twice over one fixture pair: once with a
// library document in the CommandContext (flipped path) and once without
// (legacy path, which is also what the no-bus dispatch and a legacy-parser
// fallback load still take).

#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/commands/acp_commands.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/commands/gid_commands.h"
#include "core/commands/proposal_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/commands/tree_commands.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "core/reviews/review_proposal.h"
#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kSampleSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

std::filesystem::path MakeTempProjectRoot(const std::string& tag) {
    auto root = std::filesystem::temp_directory_path() /
                ("af_liveflip_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// One audited project plus the state a CommandContext needs. `document` is null
// for the legacy-path fixture, which is exactly how a legacy-parser-fallback
// load (and the no-bus dispatch path) reaches the commands.
struct EditFixture {
    core::AssuranceProject project;
    std::filesystem::path sacm_abs;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
    std::unique_ptr<sacm_adapter::LibraryDocument> document;
    std::unique_ptr<core::commands::CommandBus> bus;
};

// Builds a project whose in-memory views are derived exactly the way
// `AppState::load_file` derives them, so both sides start from the identical
// state and only the edit routing differs.
std::unique_ptr<EditFixture> MakeFixture(const std::string& tag, bool library_backed) {
    auto fixture = std::make_unique<EditFixture>();
    const auto root = MakeTempProjectRoot(tag);
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);

    fixture->project.id = "p";
    fixture->project.name = "Project";
    fixture->project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    fixture->project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(fixture->project, sacm_rel, ensure, error)) << error;
    fixture->sacm_abs = fixture->project.rootPath / sacm_rel;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture->sacm_abs);
    EXPECT_TRUE(loaded.ok);
    EXPECT_NE(loaded.document, nullptr);
    if (loaded.document == nullptr)
        return fixture;
    core::RebuildDerivedViewsFromLibrary(*loaded.document, fixture->model, fixture->package);
    if (library_backed)
        fixture->document = std::move(loaded.document);

    fixture->bus = core::commands::CommandBus::Open(fixture->project, fixture->sacm_abs, error);
    EXPECT_TRUE(fixture->bus) << error;
    return fixture;
}

core::commands::CommandContext MakeContext(EditFixture& fixture) {
    return core::commands::CommandContext{fixture.model, fixture.package, fixture.document.get()};
}

std::string CanonicalHash(const EditFixture& fixture) {
    const std::optional<std::string> hash = core::library_canonical_hash(fixture.package);
    EXPECT_TRUE(hash.has_value());
    return hash.value_or(std::string{});
}

// Run a command through the bus, then mirror the app's frame boundary. The bus no
// longer rebuilds the live views for a FLIPPED command (that wholesale replace
// mid-dispatch was the create-a-Claim crash) -- it derives what it saves/hashes
// into a scratch copy and leaves loaded_case/sacm_package untouched. The real app
// re-derives them from the library at the next frame's top
// (AppRuntime::RebuildDerivedViewsIfNeeded). These tests run several commands
// back-to-back with no frame in between, and a flipped command PLANS ITS IDS from
// ctx.model, so the views must be fresh before the next one: re-derive here, exactly
// as the frame boundary would. An unflipped command leaves library_primary false and
// mutated the views in place, so nothing to do.
core::commands::CommandResult
RunCommand(EditFixture& fixture, core::commands::ICommand& command, core::commands::CommandContext& ctx) {
    core::commands::CommandResult result = fixture.bus->Execute(command, ctx, "tester");
    if (ctx.library_primary && fixture.document != nullptr)
        core::RebuildDerivedViewsFromLibrary(*fixture.document, fixture.model, fixture.package);
    return result;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    const auto it = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == id;
    });
    return it == model.elements.end() ? nullptr : &*it;
}

bool HasElementOfType(const parser::AssuranceCase& model, const std::string& id, const std::string& type) {
    const parser::SacmElement* element = FindElement(model, id);
    return element != nullptr && element->type == type;
}

// The (unsorted) source order of the AssertedInference a strategy reasons over --
// the vector a sibling reorder mutates, and what the app saves/loads.
std::vector<std::string> StrategyInferenceSources(const sacm::AssuranceCasePackage& package,
                                                  const std::string& strategy_id) {
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::AssertedInference& inference : argument_package.assertedInferences) {
            if (inference.reasoning == strategy_id)
                return inference.sources;
        }
    }
    return {};
}

// Runs the shared create sequence on one fixture and reports the ids the
// commands generated, so the two routings can be compared id for id.
struct CreateSequenceIds {
    std::string top_goal;
    std::string strategy;
    std::string sub_goal_one;
    std::string sub_goal_two;
    std::string solution;
    std::string challenge;
    std::string challenge_relationship;
};

CreateSequenceIds RunCreateSequence(EditFixture& fixture) {
    core::commands::CommandContext ctx = MakeContext(fixture);
    CreateSequenceIds ids;

    core::commands::CreateTopGoalCommand top_goal;
    EXPECT_TRUE(RunCommand(fixture, top_goal, ctx).success);
    ids.top_goal = top_goal.GeneratedId();

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    EXPECT_TRUE(RunCommand(fixture, add_strategy, ctx).success);
    ids.strategy = add_strategy.GeneratedId();
    EXPECT_TRUE(add_strategy.GeneratedRelationshipId().empty());

    core::commands::CreateChildElementCommand add_sub_one(ids.strategy, core::NewElementKind::Goal);
    EXPECT_TRUE(RunCommand(fixture, add_sub_one, ctx).success);
    ids.sub_goal_one = add_sub_one.GeneratedId();

    core::commands::CreateChildElementCommand add_sub_two(ids.strategy, core::NewElementKind::Goal);
    EXPECT_TRUE(RunCommand(fixture, add_sub_two, ctx).success);
    ids.sub_goal_two = add_sub_two.GeneratedId();
    // Extending the strategy's single inference creates no relationship.
    EXPECT_TRUE(add_sub_two.GeneratedRelationshipId().empty());

    core::commands::CreateChildElementCommand add_solution(ids.sub_goal_one, core::NewElementKind::Solution);
    EXPECT_TRUE(RunCommand(fixture, add_solution, ctx).success);
    ids.solution = add_solution.GeneratedId();

    core::commands::CreateChildElementCommand add_context("G1", core::NewElementKind::Context);
    EXPECT_TRUE(RunCommand(fixture, add_context, ctx).success);

    core::commands::CreateChallengeCommand challenge(core::ArgumentTarget{core::ArgumentTarget::Kind::Element, "G1"},
                                                     core::ChallengeSourceType::CounterArgument);
    EXPECT_TRUE(RunCommand(fixture, challenge, ctx).success);
    ids.challenge = challenge.GeneratedId();
    ids.challenge_relationship = challenge.GeneratedRelationshipId();

    return ids;
}

} // namespace

// (1) The create commands, run library-primary, produce the same generated ids
// AND the same canonical model hash as the legacy mutators. Ids are the audit
// payload and the on-disk identity; the canonical hash is what the manifest
// caches and the verifier compares -- so equality on both is the byte-level
// proof the flip changed the routing and nothing else.
TEST(LibraryPrimaryEditFlip, ProposalPreflightIsIsolatedAndMatchesTheLiveLibraryPath) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("proposal_preflight", true);
    ASSERT_NE(fixture->document, nullptr);

    core::reviews::ReviewProposal proposal;
    proposal.id = "draft-promotion";
    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateElementText;
    core::reviews::ElementRef element;
    element.existing_id = "G1";
    update.element = element;
    update.field = "description";
    update.new_value = "The system remains acceptably safe.";
    proposal.operations.push_back(update);

    const sacm_adapter::SaveOutcome before = sacm_adapter::save_document(*fixture->document);
    ASSERT_TRUE(before.ok);

    parser::AssuranceCase rehearsed;
    std::string error;
    ASSERT_TRUE(core::commands::PreflightProposalAgainstLibrary(*fixture->document, proposal, {}, rehearsed, error))
        << error;
    const parser::SacmElement* rehearsed_goal = FindElement(rehearsed, "G1");
    ASSERT_NE(rehearsed_goal, nullptr);
    EXPECT_EQ(rehearsed_goal->description, update.new_value);

    const sacm_adapter::SaveOutcome after_preflight = sacm_adapter::save_document(*fixture->document);
    ASSERT_TRUE(after_preflight.ok);
    EXPECT_EQ(after_preflight.xml, before.xml) << "preflight mutated the authoritative document";

    core::commands::ApplyProposalCommand command(proposal);
    core::commands::CommandContext context = MakeContext(*fixture);
    const core::commands::CommandResult committed = RunCommand(*fixture, command, context);
    ASSERT_TRUE(committed.success) << committed.error;
    EXPECT_EQ(core::reviews::ComputeModelSemanticHash(fixture->model),
              core::reviews::ComputeModelSemanticHash(rehearsed));
}

TEST(LibraryPrimaryEditFlip, CreateSequenceMatchesLegacyIdsAndCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("create_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("create_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);

    const CreateSequenceIds library_ids = RunCreateSequence(*library_side);
    const CreateSequenceIds legacy_ids = RunCreateSequence(*legacy_side);

    EXPECT_EQ(library_ids.top_goal, legacy_ids.top_goal);
    EXPECT_EQ(library_ids.strategy, legacy_ids.strategy);
    EXPECT_EQ(library_ids.sub_goal_one, legacy_ids.sub_goal_one);
    EXPECT_EQ(library_ids.sub_goal_two, legacy_ids.sub_goal_two);
    EXPECT_EQ(library_ids.solution, legacy_ids.solution);
    EXPECT_EQ(library_ids.challenge, legacy_ids.challenge);
    EXPECT_EQ(library_ids.challenge_relationship, legacy_ids.challenge_relationship);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2) A delete whose relationships fall away with the deleted elements is
// library-primary: the seam deletes one element at a time and cascades the
// relationships that reference it, which for this shape IS the legacy
// scrub-then-drop. The removed solution's evidence relationship has no other
// source, so both routings must hash identically.
TEST(LibraryPrimaryEditFlip, RemoveLeafMatchesLegacyCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("remove_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("remove_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        const CreateSequenceIds ids = RunCreateSequence(fixture);
        core::commands::CommandContext ctx = MakeContext(fixture);
        core::commands::RemoveElementCommand remove(ids.solution, core::RemoveMode::NodeAndDescendants);
        EXPECT_TRUE(RunCommand(fixture, remove, ctx).success);
        EXPECT_EQ(remove.RemovedCount(), 1u);
        EXPECT_EQ(FindElement(fixture.model, ids.solution), nullptr);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2b) The GSN single-inference encoding gives a strategy ONE AssertedInference
// whose sources are all its sub-goals. Deleting one sub-goal must leave that
// inference in place with the surviving sub-goal. A naive cascade delete (drop
// every relationship referencing the deleted element) would take the whole
// inference -- silently detaching the strategy and its remaining sub-goals from
// the argument. The library delete instead SCRUBS
// (ReferenceDeletePolicy::ScrubReferences): it removes the deleted sub-goal from
// the inference's sources and keeps the relationship because a source remains,
// reproducing the legacy scrub-then-drop, so the library-primary delete converges
// with the legacy one on this shape without any fallback.
TEST(LibraryPrimaryEditFlip, RemoveKeepsSiblingUnderSharedStrategyInference) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("shared_inf_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("shared_inf_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        const CreateSequenceIds ids = RunCreateSequence(fixture);
        core::commands::CommandContext ctx = MakeContext(fixture);

        core::commands::RemoveElementCommand remove(ids.sub_goal_one, core::RemoveMode::NodeAndDescendants);
        EXPECT_TRUE(RunCommand(fixture, remove, ctx).success);

        // The strategy's inference survives, now sourced by the other sub-goal.
        bool inference_survives = false;
        for (const sacm::ArgumentPackage& argument_package : fixture.package.argumentPackages) {
            for (const sacm::AssertedInference& inference : argument_package.assertedInferences) {
                if (inference.reasoning != ids.strategy)
                    continue;
                inference_survives = true;
                EXPECT_EQ(inference.sources, std::vector<std::string>{ids.sub_goal_two});
            }
        }
        EXPECT_TRUE(inference_survives) << "deleting one sub-goal detached the strategy from the argument";

        // A flipped command right after the fallback: its rebuild reads the
        // library, so this only holds if the library stayed in step.
        core::commands::CreateChildElementCommand add_goal(ids.sub_goal_two, core::NewElementKind::Solution);
        EXPECT_TRUE(RunCommand(fixture, add_goal, ctx).success);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2c) NodeOnly removal of an interior goal REPARENTS its children onto the
// grandparent -- legacy core::RemoveElement RETARGETS the child's inference from
// the removed node to the parent. That retarget is not a delete, so the pure
// delete+scrub seam cannot reproduce it (it would leave the child inference
// target-less, drop it, and orphan the grandchild). The flip must route NodeOnly
// through the legacy mutator and re-derive the library, NOT the seam. Both routings
// of `G1 -> E -> C`, remove E NodeOnly, must hash identically with C promoted to G1.
TEST(LibraryPrimaryEditFlip, RemoveNodeOnlyInteriorReparentsMatchesLegacy) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("node_only_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("node_only_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);
        core::commands::CreateChildElementCommand add_e("G1", core::NewElementKind::Goal);
        EXPECT_TRUE(RunCommand(fixture, add_e, ctx).success);
        const std::string e_id = add_e.GeneratedId();
        core::commands::CreateChildElementCommand add_c(e_id, core::NewElementKind::Goal);
        EXPECT_TRUE(RunCommand(fixture, add_c, ctx).success);
        const std::string c_id = add_c.GeneratedId();

        core::commands::RemoveElementCommand remove(e_id, core::RemoveMode::NodeOnly);
        EXPECT_TRUE(RunCommand(fixture, remove, ctx).success);
        EXPECT_FALSE(ctx.library_primary) << "NodeOnly took the library-primary seam, which cannot reparent";
        // E is gone; C survives, reparented onto G1.
        EXPECT_EQ(FindElement(fixture.model, e_id), nullptr);
        EXPECT_NE(FindElement(fixture.model, c_id), nullptr);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2d) Text edits are library-primary via a BRIDGE (not the apply_text_edit seam),
// so they reproduce the legacy two-slot content/description result exactly -- the same
// result the audit replay reproduces (which stays bridged) -- and converge with the
// legacy mutator on the canonical hash. Covers Name, Content on the description-only
// G1 (the slot-collapse case the seam would diverge on), and Content on a freshly
// created sub-goal. The bridge captures old->new from the library (the last committed
// state), so the ids/hash match the legacy mutator's.
TEST(LibraryPrimaryEditFlip, TextEditsMatchLegacyCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("text_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("text_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);

        core::commands::CreateChildElementCommand add_sub("G1", core::NewElementKind::Goal);
        EXPECT_TRUE(RunCommand(fixture, add_sub, ctx).success);
        const std::string sub_id = add_sub.GeneratedId();

        core::commands::UpdateElementTextCommand name_edit(
            "G1", core::ElementTextField::Name, "en", "Revised top goal");
        EXPECT_TRUE(RunCommand(fixture, name_edit, ctx).success);
        core::commands::UpdateElementTextCommand content_edit(
            "G1", core::ElementTextField::Content, "en", "The system is fully safe.");
        EXPECT_TRUE(RunCommand(fixture, content_edit, ctx).success);
        core::commands::UpdateElementTextCommand sub_content(
            sub_id, core::ElementTextField::Content, "en", "The subsystem is acceptably safe.");
        EXPECT_TRUE(RunCommand(fixture, sub_content, ctx).success);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2e) The remaining audited commands -- terminology (create package + term, add as
// visible context, update term) -- are library-primary via the bridge, reproducing the
// legacy result exactly. Run a representative sequence library-primary vs legacy and
// converge on the canonical hash. (Package removal + proposal converge the same way and
// are covered by the replay-convergence suite.)
TEST(LibraryPrimaryEditFlip, TerminologyEditsMatchLegacyCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("term_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("term_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);

        core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "Shared definitions.");
        EXPECT_TRUE(RunCommand(fixture, create_pkg, ctx).success);
        const core::TerminologyPackageRef pkg = create_pkg.GeneratedRef();

        core::TerminologyTermDraft draft;
        draft.value = "ODD";
        draft.name = "Operational Design Domain";
        core::commands::CreateTerminologyTermCommand create_term(pkg, draft);
        EXPECT_TRUE(RunCommand(fixture, create_term, ctx).success);
        const core::TerminologyTermRef term = create_term.GeneratedRef();

        core::commands::AddTerminologyTermAsVisibleContextCommand add_visible("G1", pkg, term);
        EXPECT_TRUE(RunCommand(fixture, add_visible, ctx).success);

        core::TerminologyTermDraft updated = draft;
        updated.description = "The operating conditions under which the system is designed to function.";
        core::commands::UpdateTerminologyTermCommand update_term(pkg, term, updated);
        EXPECT_TRUE(RunCommand(fixture, update_term, ctx).success);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2f) ACP record CRUD (Phase 2 slice 2c-1) is library-primary via the bridge:
// AddAcp reproduces the deterministic ACP<n> id and the vendor ACP TaggedValues,
// and Upsert edits them, exactly as the legacy mutator does. Create a Solution
// (the only element kind eligible for an element ACP), add an element ACP, then
// edit its name -- run library-primary vs legacy and converge on the canonical
// hash. The AddAcp id (ACP1) matches on both sides because both plan it from the
// same one-ACP-free starting model.
TEST(LibraryPrimaryEditFlip, AcpEditsMatchLegacyCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("acp_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("acp_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);

        core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
        EXPECT_TRUE(RunCommand(fixture, add_solution, ctx).success);
        const std::string solution_id = add_solution.GeneratedId();

        core::commands::AddAcpCommand add_acp("element", solution_id);
        EXPECT_TRUE(RunCommand(fixture, add_acp, ctx).success);
        const std::string acp_id = add_acp.GeneratedAcpId();
        EXPECT_FALSE(acp_id.empty());

        parser::AcpRecord edited;
        edited.id = acp_id;
        edited.name = "Confidence in the test report";
        edited.target_kind = "element";
        edited.target_id = solution_id;
        edited.resolution_kind = "none";
        core::commands::UpsertAcpCommand upsert(edited);
        EXPECT_TRUE(RunCommand(fixture, upsert, ctx).success);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2f) Creating a confidence argument tree for an ACP -- a compound op (confidence
// ArgumentPackage + top goal + linking UpsertAcp) minting two ids -- is library-primary
// via the bridge, reproducing the legacy result. Run it library-primary vs legacy and
// converge on the canonical hash.
TEST(LibraryPrimaryEditFlip, AcpConfidenceTreeMatchesLegacyCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("acp_tree_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("acp_tree_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);

        core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
        EXPECT_TRUE(RunCommand(fixture, add_solution, ctx).success);
        const std::string solution_id = add_solution.GeneratedId();

        core::commands::AddAcpCommand add_acp("element", solution_id);
        EXPECT_TRUE(RunCommand(fixture, add_acp, ctx).success);
        const std::string acp_id = add_acp.GeneratedAcpId();
        EXPECT_FALSE(acp_id.empty());

        core::commands::CreateConfidenceArgumentTreeForAcpCommand create_tree(acp_id);
        EXPECT_TRUE(RunCommand(fixture, create_tree, ctx).success);
        EXPECT_FALSE(create_tree.GeneratedArgumentPackageId().empty());
        EXPECT_FALSE(create_tree.GeneratedTopGoalId().empty());
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2g) The structural tree drop (reorder siblings + move a subtree) is library-
// primary like every other audited command. Run a reorder and a move library-primary
// vs legacy and converge on the canonical hash. The move makes the sequence non-vacuous
// for the (order-insensitive) canonical hash; the explicit source-order check confirms
// the library-primary REORDER preserved the sibling order on the model the flipped side
// saves (the library), which is the data loss the audit fix prevents -- and which the
// canonical hash alone would not catch because it sorts sources.
TEST(LibraryPrimaryEditFlip, TreeDropMatchesLegacyCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("tree_drop_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("tree_drop_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);

        core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
        EXPECT_TRUE(RunCommand(fixture, add_strategy, ctx).success);
        const std::string strategy_id = add_strategy.GeneratedId();
        core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
        EXPECT_TRUE(RunCommand(fixture, add_sub1, ctx).success);
        const std::string sub1_id = add_sub1.GeneratedId();
        core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
        EXPECT_TRUE(RunCommand(fixture, add_sub2, ctx).success);
        const std::string sub2_id = add_sub2.GeneratedId();

        // Reorder sub2 above sub1. Library-primary on the flipped side -- the reorder
        // must land in the library, not just the transient views.
        core::commands::ReorderSiblingsCommand reorder(sub2_id, sub1_id, core::TreeDropMode::Before);
        EXPECT_TRUE(RunCommand(fixture, reorder, ctx).success);
        EXPECT_EQ(StrategyInferenceSources(fixture.package, strategy_id), (std::vector<std::string>{sub2_id, sub1_id}))
            << "the library-primary reorder did not reach the saved model";

        // A move changes the relationship structure, so it moves the (order-insensitive)
        // canonical hash off the start value -- both routings must agree on the result.
        core::commands::CreateChildElementCommand add_c("G1", core::NewElementKind::Goal);
        EXPECT_TRUE(RunCommand(fixture, add_c, ctx).success);
        const std::string c_id = add_c.GeneratedId();
        core::commands::MoveSubtreeCommand move(c_id, sub1_id);
        EXPECT_TRUE(RunCommand(fixture, move, ctx).success);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (2h) Assigning a SACM gid to an element that lacks one is library-primary via
// the bridge, reproducing the legacy in-place SetElementGid exactly. The gid is
// RANDOM, so a fresh command per side would mint two different values and could
// not converge; instead ONE command instance is reused across both routings so it
// forces the SAME recorded gid on each (the "generated ONCE" contract). Create a
// solution (no gid), assign a gid library-primary vs legacy, and converge on the
// canonical hash -- gids contribute to that hash, so equality proves the routings
// agree on the assigned gid.
TEST(LibraryPrimaryEditFlip, GidAssignmentMatchesLegacyCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("gid_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("gid_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);

    // Create the gid-less element on both sides (deterministic ids, so the same id).
    const auto create_solution = [](EditFixture& fixture) -> std::string {
        core::commands::CommandContext ctx = MakeContext(fixture);
        core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
        EXPECT_TRUE(RunCommand(fixture, add_solution, ctx).success);
        const parser::SacmElement* created = FindElement(fixture.model, add_solution.GeneratedId());
        EXPECT_NE(created, nullptr);
        if (created != nullptr)
            EXPECT_TRUE(created->gid.empty()) << "a freshly created element unexpectedly has a gid";
        return add_solution.GeneratedId();
    };
    const std::string library_solution = create_solution(*library_side);
    const std::string legacy_solution = create_solution(*legacy_side);
    ASSERT_EQ(library_solution, legacy_solution);

    // ONE command instance drives BOTH routings so it forces the same generated gid.
    core::commands::EnsureElementGidCommand assign_gid(library_solution);

    core::commands::CommandContext library_ctx = MakeContext(*library_side);
    ASSERT_TRUE(RunCommand(*library_side, assign_gid, library_ctx).success);
    EXPECT_TRUE(library_ctx.library_primary) << "the gid assignment did not flip to the library";
    ASSERT_FALSE(assign_gid.GeneratedGid().empty());

    core::commands::CommandContext legacy_ctx = MakeContext(*legacy_side);
    ASSERT_TRUE(RunCommand(*legacy_side, assign_gid, legacy_ctx).success);
    EXPECT_FALSE(legacy_ctx.library_primary) << "the legacy side unexpectedly flipped";

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// (3) The rebuilt render model still carries the bare-strategy placement pass. A
// GSN strategy is stored as an ArgumentReasoning with a `strategyTarget` tag and
// NO inference until its first sub-goal, so the render model needs the
// synthesized sourceless inference or the strategy floats unplaced on the canvas.
// The legacy mutator pushed that placeholder itself; after the flip it can only
// come from the rebuild, which is what this pins.
TEST(LibraryPrimaryEditFlip, RebuiltModelKeepsBareStrategyPlacement) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("bare_strategy", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(RunCommand(*fixture, add_strategy, ctx).success);
    const std::string strategy_id = add_strategy.GeneratedId();

    ASSERT_TRUE(HasElementOfType(fixture->model, strategy_id, "argumentreasoning"));
    const parser::SacmElement* placeholder = FindElement(fixture->model, strategy_id + "__pending_inference");
    ASSERT_NE(placeholder, nullptr) << "bare strategy lost its synthesized placement";
    EXPECT_EQ(placeholder->type, "assertedinference");
    EXPECT_EQ(placeholder->reasoning_ref, strategy_id);
    ASSERT_EQ(placeholder->target_refs.size(), 1u);
    EXPECT_EQ(placeholder->target_refs.front(), "G1");

    // The first sub-goal materializes the real inference, and the placeholder
    // must then be gone (otherwise the strategy renders twice).
    core::commands::CreateChildElementCommand add_sub(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(RunCommand(*fixture, add_sub, ctx).success);
    EXPECT_EQ(FindElement(fixture->model, strategy_id + "__pending_inference"), nullptr);
    const parser::SacmElement* inference = FindElement(fixture->model, add_sub.GeneratedRelationshipId());
    ASSERT_NE(inference, nullptr);
    EXPECT_EQ(inference->reasoning_ref, strategy_id);
}

// (4) The rebuilt render model still carries the terminology passes. Associating
// a term with an element creates an ArtifactReference + AssertedContext that the
// UI must NOT draw as a context node; `HideTerminologyArtifactReferences` removes
// them from the render model. The association command is now library-primary (via
// the bridge), so the re-derive already runs after it -- this proves that re-derive,
// and every later flipped command's re-derive, keeps the artifact reference hidden in
// the render model while the library still owns it (it stays in the projected package,
// and is therefore still saved).
TEST(LibraryPrimaryEditFlip, RebuiltModelKeepsTerminologyHiddenButLibraryKeepsIt) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("terminology", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTerminologyPackageCommand create_package("Terms", "Shared definitions.");
    ASSERT_TRUE(RunCommand(*fixture, create_package, ctx).success);
    const core::TerminologyPackageRef package_ref = create_package.GeneratedRef();

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    core::commands::CreateTerminologyTermCommand create_term(package_ref, draft);
    ASSERT_TRUE(RunCommand(*fixture, create_term, ctx).success);

    core::commands::AssociateTerminologyTermWithElementCommand associate("G1", package_ref, create_term.GeneratedRef());
    ASSERT_TRUE(RunCommand(*fixture, associate, ctx).success);
    const std::string artifact_reference_id = associate.Result().artifact_reference_id;
    ASSERT_FALSE(artifact_reference_id.empty());
    EXPECT_EQ(FindElement(fixture->model, artifact_reference_id), nullptr)
        << "the terminology artifact reference must be hidden from the render model";

    // A FLIPPED command now rebuilds both views from the library.
    core::commands::CreateChildElementCommand add_goal("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(RunCommand(*fixture, add_goal, ctx).success);

    EXPECT_EQ(FindElement(fixture->model, artifact_reference_id), nullptr)
        << "the rebuild resurrected a terminology artifact reference as a drawn node";
    // ... while the library still owns it: the projected package (what gets
    // saved) keeps the reference and the term.
    bool package_has_reference = false;
    for (const sacm::ArgumentPackage& argument_package : fixture->package.argumentPackages) {
        for (const sacm::ArtifactReference& reference : argument_package.artifactReferences) {
            if (reference.id == artifact_reference_id)
                package_has_reference = true;
        }
    }
    EXPECT_TRUE(package_has_reference) << "UI-hidden terminology reference was dropped from the model";
    EXPECT_FALSE(fixture->package.terminologyPackages.empty());
}

// The CommandContext is reused across commands, and a flipped command leaves
// `library_primary` set. The bus MUST reset it at the start of every Execute --
// otherwise a following LEGACY command (the kill switch, or a legacy-parser fallback)
// inherits the stale flag, and the bus serializes from the scratch library projection
// the previous flipped command set up (which the legacy edit never touched) instead of
// from ctx.package, dropping the legacy edit ON DISK. All audited commands can now
// flip, so the legacy path is forced here via allow_library_primary=false. This checks
// the SAVED file (what the bus actually serialized), which is what the reset protects.
// (Regression guard for the reset in CommandBus::Execute.)
TEST(LibraryPrimaryEditFlip, LegacyCommandAfterFlippedKeepsItsEditOnDisk) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("mixed_flip", /*library_backed=*/true);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    // Flipped: mutates the library and sets library_primary.
    core::commands::CreateChildElementCommand add_goal("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(RunCommand(*fixture, add_goal, ctx).success);
    const std::string goal_id = add_goal.GeneratedId();

    // Force the next command onto the legacy path.
    ctx.allow_library_primary = false;
    core::commands::CreateTerminologyPackageCommand add_terms("Terms", "Shared definitions.");
    ASSERT_TRUE(RunCommand(*fixture, add_terms, ctx).success);
    EXPECT_FALSE(ctx.library_primary) << "the second command should have taken the legacy path";

    // The SAVED file (what the bus serialized) must reflect BOTH edits.
    const sacm_adapter::LoadOutcome saved = sacm_adapter::load_document(fixture->sacm_abs);
    ASSERT_TRUE(saved.ok);
    ASSERT_NE(saved.document, nullptr);
    const sacm::AssuranceCasePackage on_disk = core::project_library_package(*saved.document);

    bool has_goal = false;
    for (const auto& argument_package : on_disk.argumentPackages)
        for (const auto& claim : argument_package.claims)
            if (claim.id == goal_id)
                has_goal = true;
    EXPECT_TRUE(has_goal) << "the flipped create was lost on disk";

    std::size_t term_count = on_disk.terminologyPackages.size();
    for (const auto& argument_package : on_disk.argumentPackages)
        term_count += argument_package.terminologyPackages.size();
    EXPECT_EQ(term_count, 1u) << "the legacy terminology edit was clobbered by a stale library_primary";
}

// The interactive app disables the flip via CommandContext::allow_library_primary
// (a GUI re-entrancy hotfix); with it false, a flippable command must take the
// legacy in-place path instead -- it never sets library_primary -- while still
// producing the element. Guards the kill switch.
TEST(LibraryPrimaryEditFlip, AllowLibraryPrimaryFalseTakesLegacyPath) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("allow_false", /*library_backed=*/true);
    core::commands::CommandContext ctx = MakeContext(*fixture);
    ctx.allow_library_primary = false;

    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(RunCommand(*fixture, cmd, ctx).success);
    EXPECT_FALSE(ctx.library_primary) << "the flip engaged despite allow_library_primary=false";

    bool found = false;
    for (const auto& argument_package : fixture->package.argumentPackages)
        for (const auto& claim : argument_package.claims)
            if (claim.id == cmd.GeneratedId())
                found = true;
    EXPECT_TRUE(found) << "the legacy-path create did not persist";
}

// The core of the re-entrancy fix (the create-a-Claim crash). A FLIPPED command
// must NOT rebuild the live loaded_case/sacm_package inside the bus: the canvas
// renders from &loaded_case across the whole frame, and a context-menu edit
// dispatches mid-render, so replacing those containers in-dispatch frees what the
// canvas is still iterating. Assert the bus leaves the passed-in views untouched
// after a flipped command (same element count, same hash), while the library holds
// the edit; the app surfaces it at the next frame boundary (the RebuildDerived-
// ViewsFromLibrary call below, which RunCommand performs elsewhere). Dispatching
// straight through the bus here -- NOT RunCommand -- reproduces the exact window
// the canvas renders in.
TEST(LibraryPrimaryEditFlip, FlippedCommandLeavesLiveViewsUntouchedUntilRebuild) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("no_mid_dispatch_rebuild", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    const std::size_t elements_before = fixture->model.elements.size();
    const std::string hash_before = CanonicalHash(*fixture);

    core::commands::CreateChildElementCommand add_goal("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(fixture->bus->Execute(add_goal, ctx, "tester").success);
    ASSERT_TRUE(ctx.library_primary) << "expected the create to flip to the library";

    // Untouched by the bus -- so any reference the UI held into them survives.
    EXPECT_EQ(fixture->model.elements.size(), elements_before)
        << "the bus rebuilt loaded_case mid-dispatch (the re-entrancy hazard)";
    EXPECT_EQ(CanonicalHash(*fixture), hash_before)
        << "the bus rebuilt sacm_package mid-dispatch (the re-entrancy hazard)";

    // The edit lives in the library; the frame-boundary re-derive surfaces it.
    core::RebuildDerivedViewsFromLibrary(*fixture->document, fixture->model, fixture->package);
    EXPECT_GT(fixture->model.elements.size(), elements_before)
        << "the deferred re-derive did not surface the committed edit";
    EXPECT_NE(CanonicalHash(*fixture), hash_before);
    bool has_new = false;
    for (const auto& argument_package : fixture->package.argumentPackages)
        for (const auto& claim : argument_package.claims)
            if (claim.id == add_goal.GeneratedId())
                has_new = true;
    EXPECT_TRUE(has_new) << "the committed claim is missing after the re-derive";
}
