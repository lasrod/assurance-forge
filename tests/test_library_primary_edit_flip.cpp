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
#include "core/audit/replay_verifier.h"
#include "core/commands/acp_commands.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/commands/gid_commands.h"
#include "core/commands/package_commands.h"
#include "core/commands/proposal_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/commands/tree_commands.h"
#include "core/app_state.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/problems/gsn_wellformedness.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "core/reviews/review_proposal.h"
#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_parser.h"
#include "sacm_adapter/document_edit.h"
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
std::unique_ptr<EditFixture>
MakeFixture(const std::string& tag, bool library_backed, const char* sacm_xml = kSampleSacm) {
    auto fixture = std::make_unique<EditFixture>();
    const auto root = MakeTempProjectRoot(tag);
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, sacm_xml);

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
// target-less, drop it, and orphan the grandchild). NodeOnly therefore routes
// through the GUARDED bridge (project -> legacy reparent -> reload), the same
// path the audit replayer uses -- library-primary, but never the scrub seam.
// Both routings of `G1 -> E -> C`, remove E NodeOnly, must hash identically
// with C promoted to G1.
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
        if (fixture.document != nullptr) {
            EXPECT_TRUE(ctx.library_primary)
                << "NodeOnly bypassed the guarded bridge -- the unflipped path autosaves lossy projection bytes";
        }
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

// Phase 1 of the bridge retirement flipped the ten terminology commands and
// RemoveArgumentPackage onto their native seams, so the test above no longer
// measures the bridge. It also no longer covers everything that moved: category
// CRUD, the non-visible association, and the two deletes were never in it. Run
// the whole flipped tranche -- create, update and delete at all three levels plus
// both association forms -- library-primary against legacy, on the quantity the
// audit manifest caches.
//
// Deleting a term is deliberately last: it is the one edit where the seam and the
// legacy mutator part company. `core::DeleteTerminologyTerm` erases the term and
// leaves any ArtifactReference still naming it dangling; the seam's
// DeleteReferencingRelationships policy scrubs the reference. The term deleted
// here is therefore an UNREFERENCED one, where the two coincide; the referenced
// case is pinned separately by
// TerminologyTermDeleteScrubsTheReferenceTheLegacyMutatorLeftDangling below.
TEST(LibraryPrimaryEditFlip, FlippedTerminologyTrancheMatchesLegacyCanonicalHash) {
    std::unique_ptr<EditFixture> library_side = MakeFixture("term_all_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("term_all_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);

        core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "Shared definitions.");
        EXPECT_TRUE(RunCommand(fixture, create_pkg, ctx).success);
        const core::TerminologyPackageRef pkg = create_pkg.GeneratedRef();

        core::commands::UpdateTerminologyPackageCommand update_pkg(pkg, "Glossary", "Project-wide definitions.");
        EXPECT_TRUE(RunCommand(fixture, update_pkg, ctx).success);

        core::TerminologyCategoryDraft category_draft;
        category_draft.name = "Domain";
        core::commands::CreateTerminologyCategoryCommand create_category(pkg, category_draft);
        EXPECT_TRUE(RunCommand(fixture, create_category, ctx).success);
        const core::TerminologyCategoryRef category = create_category.GeneratedRef();

        core::TerminologyCategoryDraft renamed_category;
        renamed_category.name = "Operating domain";
        renamed_category.description = "Terms describing where the system operates.";
        core::commands::UpdateTerminologyCategoryCommand update_category(pkg, category, renamed_category);
        EXPECT_TRUE(RunCommand(fixture, update_category, ctx).success);

        core::TerminologyTermDraft draft;
        draft.value = "ODD";
        draft.name = "Operational Design Domain";
        draft.category_refs.push_back(category.id);
        core::commands::CreateTerminologyTermCommand create_term(pkg, draft);
        EXPECT_TRUE(RunCommand(fixture, create_term, ctx).success);
        const core::TerminologyTermRef term = create_term.GeneratedRef();

        core::commands::AssociateTerminologyTermWithElementCommand associate("G1", pkg, term);
        EXPECT_TRUE(RunCommand(fixture, associate, ctx).success);

        core::commands::AddTerminologyTermAsVisibleContextCommand add_visible("G1", pkg, term);
        EXPECT_TRUE(RunCommand(fixture, add_visible, ctx).success);

        core::TerminologyTermDraft updated = draft;
        updated.description = "The operating conditions the system is designed for.";
        core::commands::UpdateTerminologyTermCommand update_term(pkg, term, updated);
        EXPECT_TRUE(RunCommand(fixture, update_term, ctx).success);

        // An unreferenced second term, created only so its delete can be compared.
        core::TerminologyTermDraft spare;
        spare.value = "MRC";
        spare.name = "Minimal Risk Condition";
        core::commands::CreateTerminologyTermCommand create_spare(pkg, spare);
        EXPECT_TRUE(RunCommand(fixture, create_spare, ctx).success);
        core::commands::DeleteTerminologyTermCommand delete_spare(pkg, create_spare.GeneratedRef());
        EXPECT_TRUE(RunCommand(fixture, delete_spare, ctx).success);

        // The category is still assigned to the ODD term, so clear it first --
        // both paths refuse a category that is in use.
        core::TerminologyTermDraft uncategorized = updated;
        uncategorized.category_refs.clear();
        core::commands::UpdateTerminologyTermCommand drop_category(pkg, term, uncategorized);
        EXPECT_TRUE(RunCommand(fixture, drop_category, ctx).success);
        core::commands::DeleteTerminologyCategoryCommand delete_category(pkg, category);
        EXPECT_TRUE(RunCommand(fixture, delete_category, ctx).success);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// Deleting a term that an argument package still references, WITHOUT the user
// having consented to the cascade. The three possible behaviours:
//
//   legacy   -- erases the term and leaves the ArtifactReference naming an id
//               that no longer resolves. A dangling reference in a saved case.
//   seam     -- refuses with SACM-CMD-007: removing the reference crosses a
//               package boundary, and the library cascades only on opt-in.
//   seam+opt -- removes both, which is what the confirmation dialog asks for
//               (TerminologyTermDeleteWithConsentRemovesTheReferencesToo below).
//
// This pins the middle one: the default is still a refusal, so a caller that
// never asked the user cannot cascade by accident. That default matters more than
// it looks -- it is also what the audit REPLAY does for every event recorded
// before the cascade existed, so old logs keep replaying under the behaviour they
// were written under.
TEST(LibraryPrimaryEditFlip, TerminologyTermDeleteRefusesWhileAnArgumentPackageStillReferencesIt) {
    struct Attempt {
        bool delete_succeeded = false;
        std::string reference_id;
        bool term_survives = false;
        bool reference_survives = false;
        std::string error;
    };
    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);
        Attempt attempt;

        core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "");
        EXPECT_TRUE(RunCommand(fixture, create_pkg, ctx).success);
        core::TerminologyTermDraft draft;
        draft.value = "ODD";
        core::commands::CreateTerminologyTermCommand create_term(create_pkg.GeneratedRef(), draft);
        EXPECT_TRUE(RunCommand(fixture, create_term, ctx).success);
        core::commands::AssociateTerminologyTermWithElementCommand associate(
            "G1", create_pkg.GeneratedRef(), create_term.GeneratedRef());
        EXPECT_TRUE(RunCommand(fixture, associate, ctx).success);
        attempt.reference_id = associate.Result().artifact_reference_id;

        core::commands::DeleteTerminologyTermCommand delete_term(create_pkg.GeneratedRef(), create_term.GeneratedRef());
        const core::commands::CommandResult deleted = RunCommand(fixture, delete_term, ctx);
        attempt.delete_succeeded = deleted.success;
        attempt.error = deleted.error;

        attempt.term_survives = core::FindTerminologyTerm(
                                    fixture.package, create_pkg.GeneratedRef(), create_term.GeneratedRef()) != nullptr;
        for (const sacm::ArgumentPackage& argument_package : fixture.package.argumentPackages)
            for (const sacm::ArtifactReference& reference : argument_package.artifactReferences)
                if (reference.id == attempt.reference_id)
                    attempt.reference_survives = true;
        return attempt;
    };

    std::unique_ptr<EditFixture> library_side = MakeFixture("term_referenced_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("term_referenced_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);
    const Attempt flipped = run(*library_side);
    const Attempt legacy = run(*legacy_side);

    ASSERT_FALSE(flipped.reference_id.empty());
    ASSERT_EQ(flipped.reference_id, legacy.reference_id) << "the two paths minted different reference ids";

    // Non-vacuity: the legacy mutator really does accept this and leave the husk,
    // so the refusal below is a measured difference and not a fixture artefact.
    EXPECT_TRUE(legacy.delete_succeeded);
    EXPECT_FALSE(legacy.term_survives);
    EXPECT_TRUE(legacy.reference_survives) << "the legacy delete no longer leaves a dangling reference";

    EXPECT_FALSE(flipped.delete_succeeded) << "the flipped delete cascaded across the package boundary";
    EXPECT_NE(flipped.error.find("SACM-CMD-007"), std::string::npos)
        << "the refusal is not the library's cross-package guard: " << flipped.error;
    EXPECT_TRUE(flipped.term_survives) << "the refused delete removed the term anyway";
    EXPECT_TRUE(flipped.reference_survives) << "the refused delete removed the reference anyway";
}

// The other side of that refusal: with the user's confirmed consent, the delete
// takes the references with it. Two things have to hold, and the second is the
// one an audited tool lives or dies by.
//
//  1. The preview the confirmation is built from lists exactly what then goes.
//     A dialog that says "also removes 2 elements" and then removes three is
//     worse than no dialog.
//  2. The consent is REPLAYABLE. The answer lives in the audit payload, not in
//     the model, so a replay reproduces the decision the user actually made
//     rather than re-deriving one from a later document state. Asserted by
//     verifying the project, which replays the log and compares hashes.
TEST(LibraryPrimaryEditFlip, TerminologyTermDeleteWithConsentRemovesTheReferencesToo) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("term_cascade", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "");
    ASSERT_TRUE(RunCommand(*fixture, create_pkg, ctx).success);
    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    core::commands::CreateTerminologyTermCommand create_term(create_pkg.GeneratedRef(), draft);
    ASSERT_TRUE(RunCommand(*fixture, create_term, ctx).success);
    core::commands::AddTerminologyTermAsVisibleContextCommand add_visible(
        "G1", create_pkg.GeneratedRef(), create_term.GeneratedRef());
    ASSERT_TRUE(RunCommand(*fixture, add_visible, ctx).success);
    const std::string reference_id = add_visible.Result().artifact_reference_id;
    const std::string context_id = add_visible.Result().asserted_context_id;
    ASSERT_FALSE(reference_id.empty());
    ASSERT_FALSE(context_id.empty());

    // (1) What the confirmation would show.
    const sacm_adapter::DeletePreview preview =
        sacm_adapter::preview_delete_terminology_element(*fixture->document, create_term.GeneratedRef().id);
    ASSERT_TRUE(preview.supported);
    EXPECT_TRUE(preview.can_apply) << "the cascading delete is not applicable, so the dialog would offer a lie";
    std::vector<std::string> previewed;
    for (const sacm_adapter::DeleteEffect& effect : preview.consequential)
        previewed.push_back(effect.element_id);
    std::sort(previewed.begin(), previewed.end());
    EXPECT_EQ(previewed, (std::vector<std::string>{context_id, reference_id}))
        << "the preview does not name the visible-context pair the delete removes";

    core::commands::DeleteTerminologyTermCommand delete_term(
        create_pkg.GeneratedRef(), create_term.GeneratedRef(), /*cascade_references=*/true);
    ASSERT_TRUE(RunCommand(*fixture, delete_term, ctx).success);

    // ...and what it actually removed matches, term included.
    std::vector<std::string> removed = delete_term.RemovedIds();
    std::sort(removed.begin(), removed.end());
    std::vector<std::string> expected{context_id, reference_id, create_term.GeneratedRef().id};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(removed, expected) << "the delete removed a different set than the preview promised";

    EXPECT_EQ(core::FindTerminologyTerm(fixture->package, create_pkg.GeneratedRef(), create_term.GeneratedRef()),
              nullptr);
    for (const sacm::ArgumentPackage& argument_package : fixture->package.argumentPackages) {
        for (const sacm::ArtifactReference& reference : argument_package.artifactReferences)
            EXPECT_NE(reference.id, reference_id) << "the reference the user consented to remove survived";
        for (const sacm::AssertedContext& context : argument_package.assertedContexts)
            EXPECT_NE(context.id, context_id) << "the context the user consented to remove survived";
    }

    // (2) The recorded consent replays.
    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// A term nothing references needs no consent and must not ask for any: the
// preview is empty, so the confirmation stays the plain "Delete this term?" and
// the command records `cascade_references: false`.
TEST(LibraryPrimaryEditFlip, TerminologyTermDeletePreviewIsEmptyWhenNothingReferencesTheTerm) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("term_no_cascade", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "");
    ASSERT_TRUE(RunCommand(*fixture, create_pkg, ctx).success);
    core::TerminologyTermDraft draft;
    draft.value = "MRC";
    core::commands::CreateTerminologyTermCommand create_term(create_pkg.GeneratedRef(), draft);
    ASSERT_TRUE(RunCommand(*fixture, create_term, ctx).success);

    const sacm_adapter::DeletePreview preview =
        sacm_adapter::preview_delete_terminology_element(*fixture->document, create_term.GeneratedRef().id);
    ASSERT_TRUE(preview.supported);
    EXPECT_TRUE(preview.can_apply);
    EXPECT_TRUE(preview.consequential.empty()) << "an unreferenced term reported consequences, so the dialog "
                                                  "would ask for consent it does not need";

    core::commands::DeleteTerminologyTermCommand delete_term(create_pkg.GeneratedRef(), create_term.GeneratedRef());
    ASSERT_TRUE(RunCommand(*fixture, delete_term, ctx).success);
    EXPECT_EQ(delete_term.RemovedIds(), (std::vector<std::string>{create_term.GeneratedRef().id}));
}

// The payoff Phase 1 is for: an audit log written by the flipped commands replays
// through the seams that wrote it. Before the flip the live side bridged and the
// replay side seamed, so the two only agreed where the projection happened to be
// faithful -- this asserts they are now the same code.
TEST(LibraryPrimaryEditFlip, FlippedTerminologyTrancheReplaysConvergently) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("term_replay", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "Shared definitions.");
    ASSERT_TRUE(RunCommand(*fixture, create_pkg, ctx).success);
    ASSERT_TRUE(ctx.library_primary) << "the create did not reach the library natively";

    core::TerminologyCategoryDraft category_draft;
    category_draft.name = "Domain";
    core::commands::CreateTerminologyCategoryCommand create_category(create_pkg.GeneratedRef(), category_draft);
    ASSERT_TRUE(RunCommand(*fixture, create_category, ctx).success);

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    draft.category_refs.push_back(create_category.GeneratedRef().id);
    core::commands::CreateTerminologyTermCommand create_term(create_pkg.GeneratedRef(), draft);
    ASSERT_TRUE(RunCommand(*fixture, create_term, ctx).success);

    core::commands::AddTerminologyTermAsVisibleContextCommand add_visible(
        "G1", create_pkg.GeneratedRef(), create_term.GeneratedRef());
    ASSERT_TRUE(RunCommand(*fixture, add_visible, ctx).success);
    ASSERT_TRUE(ctx.library_primary) << "the visible context did not reach the library natively";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// The base gid `gid-<id>` is not always free: gid space is independent of id
// space, so this fixture's top goal carries `gid-TP1` while `TP1` is still a
// perfectly fresh id. `core::GenerateUniqueGid` therefore disambiguates to
// `gid-TP1-2`, and a flipped create that reconstructed the gid from the id alone
// would mint a DUPLICATE `gid-TP1` and diverge from both the legacy path and the
// audit payload it just wrote. Pins that the planned gid is what reaches the
// library.
TEST(LibraryPrimaryEditFlip, TerminologyCreateKeepsTheLegacyGidWhenTheBaseFormIsTaken) {
    constexpr const char* kGidCollisionSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" gid="gid-TP1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    std::unique_ptr<EditFixture> library_side =
        MakeFixture("gid_collision_library", /*library_backed=*/true, kGidCollisionSacm);
    std::unique_ptr<EditFixture> legacy_side =
        MakeFixture("gid_collision_legacy", /*library_backed=*/false, kGidCollisionSacm);
    ASSERT_NE(library_side->document, nullptr);

    // Non-vacuity: the collision has to have survived the load, or the planned gid
    // is the base form and this asserts nothing.
    const parser::SacmElement* goal = FindElement(library_side->model, "G1");
    ASSERT_NE(goal, nullptr);
    ASSERT_EQ(goal->gid, "gid-TP1") << "the fixture's colliding gid did not survive the load";

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);
        core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "");
        EXPECT_TRUE(RunCommand(fixture, create_pkg, ctx).success);
        return create_pkg.GeneratedRef();
    };
    const core::TerminologyPackageRef library_ref = run(*library_side);
    const core::TerminologyPackageRef legacy_ref = run(*legacy_side);

    EXPECT_EQ(library_ref.id, "TP1");
    EXPECT_EQ(legacy_ref.gid, "gid-TP1-2") << "the legacy generator no longer disambiguates; fixture is stale";
    EXPECT_EQ(library_ref.gid, legacy_ref.gid) << "the flipped create minted a different gid than the legacy one";

    // And the payload's claim has to be true of the document, not just of the ref.
    const sacm::AssuranceCasePackage& projected = library_side->package;
    ASSERT_EQ(projected.terminologyPackages.size(), 1u);
    EXPECT_EQ(projected.terminologyPackages.front().gid, library_ref.gid)
        << "the audit payload records a gid the saved document does not carry";
    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));
}

// RemoveArgumentPackage moved onto `apply_delete_package` in the same phase.
TEST(LibraryPrimaryEditFlip, RemoveArgumentPackageMatchesLegacyCanonicalHash) {
    constexpr const char* kTwoPackageSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
  <argumentPackage id="AP2" name="Spare">
    <claim id="G2" name="Spare goal" description="Retired branch."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    std::unique_ptr<EditFixture> library_side =
        MakeFixture("remove_ap_library", /*library_backed=*/true, kTwoPackageSacm);
    std::unique_ptr<EditFixture> legacy_side =
        MakeFixture("remove_ap_legacy", /*library_backed=*/false, kTwoPackageSacm);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(library_side->package.argumentPackages.size(), 2u);

    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);
        core::commands::RemoveArgumentPackageCommand remove("AP2", "");
        EXPECT_TRUE(RunCommand(fixture, remove, ctx).success);
    };
    run(*library_side);
    run(*legacy_side);

    EXPECT_EQ(library_side->package.argumentPackages.size(), 1u) << "the flipped removal did not take";
    EXPECT_EQ(FindElement(library_side->model, "G2"), nullptr) << "the removed package's claim is still projected";
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
        if (created != nullptr) {
            EXPECT_TRUE(created->gid.empty()) << "a freshly created element unexpectedly has a gid";
        }
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

// Copilot review, PR #360: the cascade plan deliberately spares an
// ArtifactReference that names the term AND something else, because deleting it
// would take a second artifact's evidence with it. But the term delete that
// follows then has a surviving cross-package referrer, and the default
// cross-package policy REJECTS -- after the plan's earlier deletes have already
// applied. The command reports failure over a half-mutated document.
//
// The right outcome is the one the preview already describes: the sole-purpose
// reference is deleted, the shared one survives with the term scrubbed out of
// it, and the term goes.
TEST(LibraryPrimaryEditFlip, TerminologyTermCascadeSparesASharedReferenceWithoutStranding) {
    constexpr const char* kSharedReferenceSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <terminologyPackage id="TP1" name="Terms">
    <term id="T1" value="ODD" name="Operational Design Domain"/>
  </terminologyPackage>
  <artifactPackage id="ARTP1" name="Evidence">
    <artifact id="A1" name="Test report"/>
  </artifactPackage>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <artifactReference id="TC1" name="ODD" referencedArtifact="T1"/>
    <assertedContext id="AC2" name="Context: ODD" source="TC1" target="G1"
                     description="assurance-forge:visible-term-context"/>
    <artifactReference id="AR_shared" name="ODD and the test report" referencedArtifact="T1 A1"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    std::unique_ptr<EditFixture> fixture =
        MakeFixture("term_shared_ref", /*library_backed=*/true, kSharedReferenceSacm);
    ASSERT_NE(fixture->document, nullptr);

    const sacm_adapter::DeletePreview preview =
        sacm_adapter::preview_delete_terminology_element(*fixture->document, "T1");
    ASSERT_TRUE(preview.supported);
    // The shared reference is reported as CHANGED, not removed: that distinction
    // is the reason the dialog lists effects rather than a bare count.
    bool shared_listed_as_modified = false;
    bool sole_purpose_listed_as_deleted = false;
    for (const sacm_adapter::DeleteEffect& effect : preview.consequential) {
        if (effect.element_id == "AR_shared")
            shared_listed_as_modified = !effect.deleted;
        if (effect.element_id == "TC1")
            sole_purpose_listed_as_deleted = effect.deleted;
    }
    EXPECT_TRUE(sole_purpose_listed_as_deleted) << "the sole-purpose reference is not listed for removal";
    EXPECT_TRUE(shared_listed_as_modified) << "the shared reference is not listed as merely changed";

    core::commands::CommandContext ctx = MakeContext(*fixture);
    core::commands::DeleteTerminologyTermCommand delete_term(
        core::TerminologyPackageRef{"TP1", ""}, core::TerminologyTermRef{"T1", ""}, /*cascade_references=*/true);
    const core::commands::CommandResult result = RunCommand(*fixture, delete_term, ctx);
    ASSERT_TRUE(result.success) << "the cascade stranded the document part-way: " << result.error;

    EXPECT_EQ(core::FindTerminologyTerm(
                  fixture->package, core::TerminologyPackageRef{"TP1", ""}, core::TerminologyTermRef{"T1", ""}),
              nullptr);
    bool shared_survives = false;
    for (const sacm::ArgumentPackage& argument_package : fixture->package.argumentPackages) {
        for (const sacm::ArtifactReference& reference : argument_package.artifactReferences) {
            EXPECT_NE(reference.id, "TC1") << "the sole-purpose reference survived";
            if (reference.id == "AR_shared") {
                shared_survives = true;
                EXPECT_EQ(reference.referencedArtifact.find("T1"), std::string::npos)
                    << "the shared reference still names the deleted term: " << reference.referencedArtifact;
            }
        }
    }
    EXPECT_TRUE(shared_survives) << "the cascade took a reference that also pointed at other evidence";
}

// Copilot review, PR #360: the delete preview was consumed without checking
// whether the delete would go through, so the dialog could list removals,
// collect consent, and then refuse. `preview_delete_terminology_element` now
// answers the caller's actual question -- "does the term go?" -- rather than the
// sequence preview's looser "did anything happen".
//
// Honest limitation, stated because the test cannot state it: the two readings
// only diverge when a sub-delete succeeds while the target's is rejected, and
// the cross-package policy fix above is what stops that arising. So this pins
// the contract on the input that is reachable today (a term the document does
// not hold) and the app-side gate on it is defence-in-depth against a future
// policy change re-opening the gap, not a guard with a live failure behind it.
TEST(LibraryPrimaryEditFlip, TerminologyDeletePreviewReportsWhetherTheTargetItselfWouldGo) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("term_preview_contract", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "");
    ASSERT_TRUE(RunCommand(*fixture, create_pkg, ctx).success);
    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    core::commands::CreateTerminologyTermCommand create_term(create_pkg.GeneratedRef(), draft);
    ASSERT_TRUE(RunCommand(*fixture, create_term, ctx).success);

    const sacm_adapter::DeletePreview real =
        sacm_adapter::preview_delete_terminology_element(*fixture->document, create_term.GeneratedRef().id);
    EXPECT_TRUE(real.can_apply) << "a term that plainly can be deleted was reported as unapplicable";

    const sacm_adapter::DeletePreview absent =
        sacm_adapter::preview_delete_terminology_element(*fixture->document, "T_not_in_this_document");
    EXPECT_FALSE(absent.can_apply) << "a delete with no target to remove reported itself applicable";
    EXPECT_TRUE(absent.consequential.empty());
}

// Phase 2a. Removing an artifact package is where the seam is CLEANER than the
// legacy mutator rather than more destructive:
//
//   legacy -- erases the package and leaves every ArtifactReference that named
//             one of its artifacts pointing at an id that no longer resolves.
//   seam   -- drops the reference to the deleted artifact, so nothing dangles.
//
// Same shape as phase 1's term-delete disclosure, opposite sign: no consent is
// needed because nothing the user can see is being removed, only a broken
// pointer. Asserted on both sides so the difference is measured, not assumed.
TEST(LibraryPrimaryEditFlip, RemoveArtifactPackageScrubsTheReferenceTheLegacyMutatorLeftDangling) {
    constexpr const char* kCitedArtifactSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <artifactPackage id="ARTP1" name="Evidence">
    <artifact id="A1" name="Brake test report"/>
  </artifactPackage>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <artifactReference id="EV1" name="Test report" referencedArtifact="A1"/>
    <assertedEvidence id="E1" source="EV1" target="G1"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);
        core::commands::RemoveArtifactPackageCommand remove("ARTP1", "");
        EXPECT_TRUE(RunCommand(fixture, remove, ctx).success);
        std::string reference_target = "<missing>";
        for (const sacm::ArgumentPackage& argument_package : fixture.package.argumentPackages)
            for (const sacm::ArtifactReference& reference : argument_package.artifactReferences)
                if (reference.id == "EV1")
                    reference_target = reference.referencedArtifact;
        return reference_target;
    };

    std::unique_ptr<EditFixture> library_side =
        MakeFixture("remove_artp_library", /*library_backed=*/true, kCitedArtifactSacm);
    std::unique_ptr<EditFixture> legacy_side =
        MakeFixture("remove_artp_legacy", /*library_backed=*/false, kCitedArtifactSacm);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);

    const std::string flipped_target = run(*library_side);
    const std::string legacy_target = run(*legacy_side);

    EXPECT_EQ(legacy_target, "A1") << "the legacy mutator no longer leaves the reference dangling; fixture is stale";
    EXPECT_TRUE(flipped_target.empty()) << "the flipped removal left the reference naming a deleted artifact: "
                                        << flipped_target;
    // The reference itself survives either way -- it is a drawn Solution node, and
    // removing evidence from the argument is not what "delete this artifact
    // package" asked for.
    EXPECT_NE(flipped_target, "<missing>") << "the removal deleted the ArtifactReference too, which overshoots";

    // The requirement that actually matters in production is not agreement with
    // the legacy mutator -- nothing replays through it any more -- but that the
    // live edit and its own replay agree. VerifyProject replays the log through
    // the seams and compares hashes, so this is that check.
    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(library_side->project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// Phase 2a. `core::DeleteTerminologyPackage` refuses a package that still holds
// terms; `apply_delete_package` deletes recursively. That guard is an Assurance
// Forge editing rule rather than a SACM invariant, so the seam has no opinion
// about it and the flip had to re-state it -- otherwise "empty this first" would
// have become "the glossary is gone" on the same click.
TEST(LibraryPrimaryEditFlip, RemoveTerminologyPackageStillRefusesANonEmptyPackage) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("remove_tp_nonempty", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTerminologyPackageCommand create_pkg("Terms", "");
    ASSERT_TRUE(RunCommand(*fixture, create_pkg, ctx).success);
    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    core::commands::CreateTerminologyTermCommand create_term(create_pkg.GeneratedRef(), draft);
    ASSERT_TRUE(RunCommand(*fixture, create_term, ctx).success);

    core::commands::RemoveTerminologyPackageCommand remove(create_pkg.GeneratedRef().id, create_pkg.GeneratedRef().gid);
    const core::commands::CommandResult result = RunCommand(*fixture, remove, ctx);
    EXPECT_FALSE(result.success) << "the flipped removal deleted a non-empty glossary recursively";
    EXPECT_NE(result.error.find("contains terms"), std::string::npos)
        << "the refusal is not the legacy guard's: " << result.error;
    EXPECT_NE(core::FindTerminologyPackage(fixture->package, create_pkg.GeneratedRef()), nullptr)
        << "the refused removal deleted the package anyway";

    // Emptied, it goes.
    core::commands::DeleteTerminologyTermCommand delete_term(create_pkg.GeneratedRef(), create_term.GeneratedRef());
    ASSERT_TRUE(RunCommand(*fixture, delete_term, ctx).success);
    core::commands::RemoveTerminologyPackageCommand remove_again(create_pkg.GeneratedRef().id,
                                                                 create_pkg.GeneratedRef().gid);
    EXPECT_TRUE(RunCommand(*fixture, remove_again, ctx).success);
    EXPECT_EQ(core::FindTerminologyPackage(fixture->package, create_pkg.GeneratedRef()), nullptr);
}

// Slice 2b. The GSN identifier is a vendor TaggedValue, and the library has
// `AddTaggedValue` but no update, so the seam composes drop-then-add. Two things
// that composition can get wrong, both asserted here: the element must end with
// exactly ONE identifier tag (an additive write would leave two, and the
// projection reads the first), and a rename must be reversible.
TEST(LibraryPrimaryEditFlip, GsnIdentifierEditsMatchLegacyCanonicalHash) {
    const auto run = [](EditFixture& fixture) {
        core::commands::CommandContext ctx = MakeContext(fixture);
        core::commands::UpdateGsnIdentifierCommand rename("G1", "TOP1");
        EXPECT_TRUE(RunCommand(fixture, rename, ctx).success);
        core::commands::UpdateGsnIdentifierCommand rename_again("G1", "TOP2");
        EXPECT_TRUE(RunCommand(fixture, rename_again, ctx).success);
        core::commands::UpdateGsnIdentifierCommand back("G1", "TOP1");
        EXPECT_TRUE(RunCommand(fixture, back, ctx).success);
    };
    std::unique_ptr<EditFixture> library_side = MakeFixture("gsn_id_library", /*library_backed=*/true);
    std::unique_ptr<EditFixture> legacy_side = MakeFixture("gsn_id_legacy", /*library_backed=*/false);
    ASSERT_NE(library_side->document, nullptr);
    ASSERT_EQ(legacy_side->document, nullptr);
    run(*library_side);
    run(*legacy_side);

    const parser::SacmElement* flipped = FindElement(library_side->model, "G1");
    ASSERT_NE(flipped, nullptr);
    EXPECT_EQ(flipped->gsn_identifier, "TOP1");
    EXPECT_EQ(CanonicalHash(*library_side), CanonicalHash(*legacy_side));

    // Exactly one tag survives three writes.
    std::size_t identifier_tags = 0;
    for (const sacm::ArgumentPackage& argument_package : library_side->package.argumentPackages)
        for (const sacm::Claim& claim : argument_package.claims)
            if (claim.id == "G1")
                for (const sacm::TaggedValue& tag : claim.taggedValues)
                    if (tag.key == core::kGsnIdentifierTagKey)
                        ++identifier_tags;
    EXPECT_EQ(identifier_tags, 1u) << "drop-then-add left the element carrying more than one identifier";
}

// The editing rules are Assurance Forge's, not SACM's, so the seam does not
// enforce them and the flipped command has to. Each of these was refused before
// the flip and must still be, with the same message.
TEST(LibraryPrimaryEditFlip, GsnIdentifierEditKeepsTheLegacyEditingRules) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("gsn_id_rules", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateChildElementCommand add_sub("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(RunCommand(*fixture, add_sub, ctx).success);
    const parser::SacmElement* sibling = FindElement(fixture->model, add_sub.GeneratedId());
    ASSERT_NE(sibling, nullptr);
    const std::string taken = sibling->gsn_identifier;
    ASSERT_FALSE(taken.empty());

    const auto refuses = [&](const std::string& identifier, const char* fragment) {
        core::commands::UpdateGsnIdentifierCommand rename("G1", identifier);
        const core::commands::CommandResult result = RunCommand(*fixture, rename, ctx);
        EXPECT_FALSE(result.success) << "accepted '" << identifier << "'";
        EXPECT_NE(result.error.find(fragment), std::string::npos) << result.error;
    };
    refuses("", "non-empty");
    refuses("  ", "non-empty");
    refuses(" G9 ", "whitespace");
    refuses(taken, "already used");

    // ...and the element is untouched by any of them.
    const parser::SacmElement* target = FindElement(fixture->model, "G1");
    ASSERT_NE(target, nullptr);
    EXPECT_NE(target->gsn_identifier, taken);
}

// Slice 2b. The undeveloped decorator, and the defect behind it.
//
// GSN `undeveloped` is SACM `assertionDeclaration = needsSupport` -- ONE enum.
// The legacy path kept `undeveloped` as a POD boolean beside the declaration and
// wrote both; the reader honours that shorthand only when the declaration is
// still `asserted`, so on a GSN Assumption the write reported success and was
// silently lost on reload. Measured before the fix: in-memory undeveloped=0,
// on-disk undeveloped=0, command success=1, status bar "Marked A1 undeveloped."
//
// Now the decorator is written where SACM actually keeps it, so it sticks; and
// where the declaration is already saying something else the edit is REFUSED
// rather than either overwriting it (turning an Assumption into an undeveloped
// Goal) or pretending.
TEST(LibraryPrimaryEditFlip, UndevelopedDecoratorSticksOnAGoalAndIsRefusedOnAnAssumption) {
    constexpr const char* kMixedDeclarationSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <claim id="A1" name="Assumed thing" assertionDeclaration="assumed"/>
    <claim id="J1" name="Justified thing" assertionDeclaration="axiomatic"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    std::unique_ptr<EditFixture> fixture = MakeFixture("undeveloped", /*library_backed=*/true, kMixedDeclarationSacm);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    // (1) An ordinary Goal: the decorator is set, and it is in the saved file as
    // the SACM declaration rather than as a boolean the reader will discard.
    core::commands::SetElementUndevelopedCommand mark("G1", true);
    ASSERT_TRUE(RunCommand(*fixture, mark, ctx).success);
    const parser::SacmElement* goal = FindElement(fixture->model, "G1");
    ASSERT_NE(goal, nullptr);
    EXPECT_TRUE(goal->undeveloped) << "the decorator did not survive the frame-boundary rebuild";
    EXPECT_EQ(goal->assertion_declaration, "needsSupport");

    core::AppState reopened;
    ASSERT_TRUE(reopened.load_file(fixture->sacm_abs.string())) << reopened.status_message;
    bool on_disk_undeveloped = false;
    for (const parser::SacmElement& element : reopened.loaded_case->elements)
        if (element.id == "G1")
            on_disk_undeveloped = element.undeveloped;
    EXPECT_TRUE(on_disk_undeveloped) << "the decorator was lost on reload -- the defect this fixes";

    // (2) An Assumption and a Justification: refused, saying why, and unchanged.
    for (const char* id : {"A1", "J1"}) {
        SCOPED_TRACE(id);
        core::commands::SetElementUndevelopedCommand refused(id, true);
        const core::commands::CommandResult result = RunCommand(*fixture, refused, ctx);
        EXPECT_FALSE(result.success) << "marking a non-asserted claim undeveloped was accepted";
        EXPECT_NE(result.error.find("assertionDeclaration"), std::string::npos)
            << "the refusal does not explain the collision: " << result.error;
        const parser::SacmElement* element = FindElement(fixture->model, id);
        ASSERT_NE(element, nullptr);
        EXPECT_FALSE(element->undeveloped);
        EXPECT_NE(element->assertion_declaration, "needsSupport")
            << "the refused edit overwrote the declaration anyway";
    }

    // (3) Clearing it returns the goal to plain asserted, and replays.
    core::commands::SetElementUndevelopedCommand clear("G1", false);
    ASSERT_TRUE(RunCommand(*fixture, clear, ctx).success);
    const parser::SacmElement* cleared = FindElement(fixture->model, "G1");
    ASSERT_NE(cleared, nullptr);
    EXPECT_FALSE(cleared->undeveloped);
    EXPECT_EQ(cleared->assertion_declaration, "asserted");

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// Phase 3a. `DropRelationshipReference` is the quick fix for an
// `UnresolvedEndpoint` finding, so the reference it drops names an element that
// does not exist -- which is why the delete seam cannot serve: there is nothing
// to delete. It needed a new library operation, `SetRelationshipEnds`.
//
// The fixture carries TWO dangling sources on one inference, which is the case
// that decided the operation's contract: had it required every id to resolve,
// dropping either would have been refused because the other was still in the
// list being written, and a two-fault relationship would be unrepairable.
TEST(LibraryPrimaryEditFlip, DropsOneBrokenEndpointAtATimeAndKeepsTheRest) {
    constexpr const char* kTwoDanglesSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <claim id="G2" name="Sub goal"/>
    <assertedInference id="R1" source="G2 gone_a gone_b" target="G1"/>
    <argumentPackage id="AP_nested" name="Nested"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    // The nested ArgumentPackage is the routing discriminator: the legacy POD has
    // no field for it, so a BRIDGED edit on this case is refused outright. Both
    // repairs below therefore only succeed if they reached the library directly.
    std::unique_ptr<EditFixture> fixture = MakeFixture("drop_ref", /*library_backed=*/true, kTwoDanglesSacm);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    const parser::SacmElement* before = FindElement(fixture->model, "R1");
    ASSERT_NE(before, nullptr);
    ASSERT_EQ(before->source_refs, (std::vector<std::string>{"G2", "gone_a", "gone_b"}))
        << "the fixture's dangling endpoints did not survive the load; this measures nothing";

    core::commands::DropRelationshipReferenceCommand drop_first("R1", "gone_a");
    ASSERT_TRUE(RunCommand(*fixture, drop_first, ctx).success);
    const parser::SacmElement* after_first = FindElement(fixture->model, "R1");
    ASSERT_NE(after_first, nullptr);
    EXPECT_EQ(after_first->source_refs, (std::vector<std::string>{"G2", "gone_b"}))
        << "the repair could not proceed with a second broken endpoint still present";

    core::commands::DropRelationshipReferenceCommand drop_second("R1", "gone_b");
    ASSERT_TRUE(RunCommand(*fixture, drop_second, ctx).success);
    const parser::SacmElement* after_second = FindElement(fixture->model, "R1");
    ASSERT_NE(after_second, nullptr);
    EXPECT_EQ(after_second->source_refs, (std::vector<std::string>{"G2"}));
    EXPECT_TRUE(core::CheckGsnWellFormedness(fixture->model).empty())
        << "the repair did not clear the finding it exists to clear";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}

// Scrubbing the LAST endpoint leaves a relationship relating nothing, so it goes
// -- and its surviving nodes do not. Withdrawing a claim of support is not a
// decision to delete what it connected.
TEST(LibraryPrimaryEditFlip, DroppingTheLastEndpointRemovesTheRelationshipNotItsNodes) {
    constexpr const char* kSingleDangleSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
    <assertedInference id="R1" source="gone_only" target="G1"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    std::unique_ptr<EditFixture> fixture = MakeFixture("drop_last", /*library_backed=*/true, kSingleDangleSacm);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);
    ASSERT_NE(FindElement(fixture->model, "R1"), nullptr);

    core::commands::DropRelationshipReferenceCommand drop("R1", "gone_only");
    ASSERT_TRUE(RunCommand(*fixture, drop, ctx).success);

    EXPECT_EQ(FindElement(fixture->model, "R1"), nullptr) << "the emptied relationship survived";
    EXPECT_NE(FindElement(fixture->model, "G1"), nullptr) << "the repair took the goal with it";

    const core::audit::ReplayVerificationResult verified = core::audit::VerifyProject(fixture->project);
    EXPECT_TRUE(verified.success) << (verified.diagnostics.empty() ? std::string{} : verified.diagnostics.front());
}
