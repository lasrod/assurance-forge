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
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
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
                ("af_liveflip_" + tag + "_" +
                 std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
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
    core::AssuranceProject                        project;
    std::filesystem::path                         sacm_abs;
    sacm::AssuranceCasePackage                    package;
    parser::AssuranceCase                         model;
    std::unique_ptr<sacm_adapter::LibraryDocument> document;
    std::unique_ptr<core::commands::CommandBus>   bus;
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

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    const auto it = std::find_if(model.elements.begin(), model.elements.end(),
                                 [&](const parser::SacmElement& element) { return element.id == id; });
    return it == model.elements.end() ? nullptr : &*it;
}

bool HasElementOfType(const parser::AssuranceCase& model, const std::string& id, const std::string& type) {
    const parser::SacmElement* element = FindElement(model, id);
    return element != nullptr && element->type == type;
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
    EXPECT_TRUE(fixture.bus->Execute(top_goal, ctx, "tester").success);
    ids.top_goal = top_goal.GeneratedId();

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    EXPECT_TRUE(fixture.bus->Execute(add_strategy, ctx, "tester").success);
    ids.strategy = add_strategy.GeneratedId();
    EXPECT_TRUE(add_strategy.GeneratedRelationshipId().empty());

    core::commands::CreateChildElementCommand add_sub_one(ids.strategy, core::NewElementKind::Goal);
    EXPECT_TRUE(fixture.bus->Execute(add_sub_one, ctx, "tester").success);
    ids.sub_goal_one = add_sub_one.GeneratedId();

    core::commands::CreateChildElementCommand add_sub_two(ids.strategy, core::NewElementKind::Goal);
    EXPECT_TRUE(fixture.bus->Execute(add_sub_two, ctx, "tester").success);
    ids.sub_goal_two = add_sub_two.GeneratedId();
    // Extending the strategy's single inference creates no relationship.
    EXPECT_TRUE(add_sub_two.GeneratedRelationshipId().empty());

    core::commands::CreateChildElementCommand add_solution(ids.sub_goal_one,
                                                           core::NewElementKind::Solution);
    EXPECT_TRUE(fixture.bus->Execute(add_solution, ctx, "tester").success);
    ids.solution = add_solution.GeneratedId();

    core::commands::CreateChildElementCommand add_context("G1", core::NewElementKind::Context);
    EXPECT_TRUE(fixture.bus->Execute(add_context, ctx, "tester").success);

    core::commands::CreateChallengeCommand challenge(
        core::ArgumentTarget{core::ArgumentTarget::Kind::Element, "G1"},
        core::ChallengeSourceType::CounterArgument);
    EXPECT_TRUE(fixture.bus->Execute(challenge, ctx, "tester").success);
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
        core::commands::RemoveElementCommand remove(ids.solution,
                                                    core::RemoveMode::NodeAndDescendants);
        EXPECT_TRUE(fixture.bus->Execute(remove, ctx, "tester").success);
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

        core::commands::RemoveElementCommand remove(ids.sub_goal_one,
                                                    core::RemoveMode::NodeAndDescendants);
        EXPECT_TRUE(fixture.bus->Execute(remove, ctx, "tester").success);

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
        EXPECT_TRUE(inference_survives)
            << "deleting one sub-goal detached the strategy from the argument";

        // A flipped command right after the fallback: its rebuild reads the
        // library, so this only holds if the library stayed in step.
        core::commands::CreateChildElementCommand add_goal(ids.sub_goal_two,
                                                           core::NewElementKind::Solution);
        EXPECT_TRUE(fixture.bus->Execute(add_goal, ctx, "tester").success);
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
        EXPECT_TRUE(fixture.bus->Execute(add_e, ctx, "tester").success);
        const std::string e_id = add_e.GeneratedId();
        core::commands::CreateChildElementCommand add_c(e_id, core::NewElementKind::Goal);
        EXPECT_TRUE(fixture.bus->Execute(add_c, ctx, "tester").success);
        const std::string c_id = add_c.GeneratedId();

        core::commands::RemoveElementCommand remove(e_id, core::RemoveMode::NodeOnly);
        EXPECT_TRUE(fixture.bus->Execute(remove, ctx, "tester").success);
        EXPECT_FALSE(ctx.library_primary)
            << "NodeOnly took the library-primary seam, which cannot reparent";
        // E is gone; C survives, reparented onto G1.
        EXPECT_EQ(FindElement(fixture.model, e_id), nullptr);
        EXPECT_NE(FindElement(fixture.model, c_id), nullptr);
    };
    run(*library_side);
    run(*legacy_side);

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
    ASSERT_TRUE(fixture->bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();

    ASSERT_TRUE(HasElementOfType(fixture->model, strategy_id, "argumentreasoning"));
    const parser::SacmElement* placeholder =
        FindElement(fixture->model, strategy_id + "__pending_inference");
    ASSERT_NE(placeholder, nullptr) << "bare strategy lost its synthesized placement";
    EXPECT_EQ(placeholder->type, "assertedinference");
    EXPECT_EQ(placeholder->reasoning_ref, strategy_id);
    ASSERT_EQ(placeholder->target_refs.size(), 1u);
    EXPECT_EQ(placeholder->target_refs.front(), "G1");

    // The first sub-goal materializes the real inference, and the placeholder
    // must then be gone (otherwise the strategy renders twice).
    core::commands::CreateChildElementCommand add_sub(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(fixture->bus->Execute(add_sub, ctx, "tester").success);
    EXPECT_EQ(FindElement(fixture->model, strategy_id + "__pending_inference"), nullptr);
    const parser::SacmElement* inference =
        FindElement(fixture->model, add_sub.GeneratedRelationshipId());
    ASSERT_NE(inference, nullptr);
    EXPECT_EQ(inference->reasoning_ref, strategy_id);
}

// (4) The rebuilt render model still carries the terminology passes. Associating
// a term with an element creates an ArtifactReference + AssertedContext that the
// UI must NOT draw as a context node; `HideTerminologyArtifactReferences` removes
// them from the render model. The association command is not flipped, so this
// also proves a flipped command rebuilding the views does not resurrect data an
// unflipped command hid -- and that the hidden elements survive in the library
// (they are still in the projected package, and therefore still saved).
TEST(LibraryPrimaryEditFlip, RebuiltModelKeepsTerminologyHiddenButLibraryKeepsIt) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("terminology", /*library_backed=*/true);
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTerminologyPackageCommand create_package("Terms", "Shared definitions.");
    ASSERT_TRUE(fixture->bus->Execute(create_package, ctx, "tester").success);
    const core::TerminologyPackageRef package_ref = create_package.GeneratedRef();

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    core::commands::CreateTerminologyTermCommand create_term(package_ref, draft);
    ASSERT_TRUE(fixture->bus->Execute(create_term, ctx, "tester").success);

    core::commands::AssociateTerminologyTermWithElementCommand associate("G1", package_ref,
                                                                         create_term.GeneratedRef());
    ASSERT_TRUE(fixture->bus->Execute(associate, ctx, "tester").success);
    const std::string artifact_reference_id = associate.Result().artifact_reference_id;
    ASSERT_FALSE(artifact_reference_id.empty());
    EXPECT_EQ(FindElement(fixture->model, artifact_reference_id), nullptr)
        << "terminology artifact reference should not be a drawn node before the flip";

    // A FLIPPED command now rebuilds both views from the library.
    core::commands::CreateChildElementCommand add_goal("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(fixture->bus->Execute(add_goal, ctx, "tester").success);

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
// `library_primary` set. The bus MUST reset that before the next command --
// otherwise an unflipped, legacy-only command that follows a flipped one has its
// edit discarded when the bus rebuilds the views from the (stale, un-updated)
// library. Here: a flipped create, then an unflipped terminology create, in ONE
// context. Both edits must survive. (Regression guard for the reset in
// CommandBus::Execute.)
TEST(LibraryPrimaryEditFlip, UnflippedCommandAfterFlippedKeepsItsEdit) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("mixed_flip", /*library_backed=*/true);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    const auto count_terminology = [](const sacm::AssuranceCasePackage& package) {
        std::size_t total = package.terminologyPackages.size();
        for (const auto& argument_package : package.argumentPackages)
            total += argument_package.terminologyPackages.size();
        return total;
    };
    ASSERT_EQ(count_terminology(fixture->package), 0u);

    // Flipped: mutates the library first and sets library_primary.
    core::commands::CreateChildElementCommand add_goal("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(fixture->bus->Execute(add_goal, ctx, "tester").success);
    const std::string goal_id = add_goal.GeneratedId();

    // Unflipped legacy-only: mutates the package via the legacy mutator. Its edit
    // must not be clobbered by a stale library_primary from the create above.
    core::commands::CreateTerminologyPackageCommand add_terms("Terms", "Shared definitions.");
    ASSERT_TRUE(fixture->bus->Execute(add_terms, ctx, "tester").success);

    EXPECT_EQ(count_terminology(fixture->package), 1u)
        << "the unflipped terminology edit was clobbered by a stale library_primary flag";
    bool has_goal = false;
    for (const auto& argument_package : fixture->package.argumentPackages)
        for (const auto& claim : argument_package.claims)
            if (claim.id == goal_id)
                has_goal = true;
    EXPECT_TRUE(has_goal) << "the flipped create was lost";
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
    ASSERT_TRUE(fixture->bus->Execute(cmd, ctx, "tester").success);
    EXPECT_FALSE(ctx.library_primary) << "the flip engaged despite allow_library_primary=false";

    bool found = false;
    for (const auto& argument_package : fixture->package.argumentPackages)
        for (const auto& claim : argument_package.claims)
            if (claim.id == cmd.GeneratedId())
                found = true;
    EXPECT_TRUE(found) << "the legacy-path create did not persist";
}
