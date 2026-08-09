// Phase 1b slice 3a: differential convergence between the legacy audit replay
// (`Replayer::ReplayFrom`, which applies events through the legacy `core::*`
// mutators) and the new library-primary replay (`Replayer::ReplayToLibrary`,
// which applies the SAME events through the `sacm_adapter` library seams).
//
// The oracle is the library canonical hash: both a genuine library document
// (replayed via the seams) and the legacy package (replayed via the mutators)
// are hashed through `core::library_canonical_hash`, which serializes and
// reloads through the library so projection-vs-legacy normalization differences
// cancel out on both sides. What CANNOT cancel is data present on one side and
// absent on the other -- which is how the two slice-3a impedances (terminology
// gid; claim content vs second-Description slot) used to surface.
//
// Slice 3b-1 resolves both by BRIDGING the divergent events: the gid-minting
// terminology CREATE/ASSOCIATE events and the `Content`/`Description` text edits
// run the SAME legacy mutator the live path uses onto a projected package, then
// re-derive the library. So every test below now converges on the RAW canonical
// hash -- no gid normalization.
//
// These tests only prove the additive path converges; no existing consumer is
// rewired here (a later slice does that).

#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/audit/event_replayer.h"
#include "core/commands/acp_commands.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/commands/gid_commands.h"
#include "core/commands/package_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/commands/tree_commands.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "core/terminology_package_service.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_parser.h"
#include "legacy_sacm/sacm_serializer.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
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
                ("af_libreplay_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct ProjectFixture {
    core::AssuranceProject project;
    std::filesystem::path sacm_abs;
    std::filesystem::path snapshot_abs;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
};

ProjectFixture MakeFixture(const std::string& tag, const char* sacm_xml = kSampleSacm) {
    ProjectFixture f;
    const auto root = MakeTempProjectRoot(tag);
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, sacm_xml);

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
    f.snapshot_abs = core::audit::SnapshotSacmPath(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto pkg = sacm::parse_sacm(f.sacm_abs.string());
    EXPECT_TRUE(pkg.has_value()) << (pkg.has_value() ? "" : pkg.error());
    if (pkg.has_value())
        f.package = std::move(pkg.value());
    auto parsed = parser::parse_sacm_xml_string(sacm_xml);
    EXPECT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error());
    if (parsed.has_value())
        f.model = std::move(parsed.value());
    return f;
}

// Legacy replay of the whole log from snapshot zero.
core::audit::ReplayState LegacyReplay(const ProjectFixture& f, const std::vector<core::audit::AuditTransaction>& txns) {
    auto snapshot_pkg = sacm::parse_sacm(f.snapshot_abs.string());
    auto snapshot_model = parser::parse_sacm_xml(f.snapshot_abs.string());
    if (!snapshot_pkg.has_value() || !snapshot_model.has_value()) {
        ADD_FAILURE() << "snapshot parse failed";
        return {};
    }
    auto replayed = core::audit::Replayer::ReplayFrom(
        *snapshot_model, *snapshot_pkg, txns, std::numeric_limits<std::uint64_t>::max());
    if (!replayed.has_value()) {
        ADD_FAILURE() << replayed.error();
        return {};
    }
    return std::move(replayed.value());
}

// Library-primary replay of the whole log from the snapshot loaded through the
// library.
std::unique_ptr<sacm_adapter::LibraryDocument> LibraryReplay(const ProjectFixture& f,
                                                             const std::vector<core::audit::AuditTransaction>& txns) {
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.snapshot_abs);
    EXPECT_TRUE(loaded.ok);
    EXPECT_NE(loaded.document, nullptr);
    auto replayed = core::audit::Replayer::ReplayToLibrary(
        std::move(loaded.document), txns, std::numeric_limits<std::uint64_t>::max());
    EXPECT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());
    if (!replayed.has_value()) {
        return nullptr;
    }
    return std::move(replayed.value());
}

// The (unsorted) source order of the single AssertedInference a strategy reasons
// over -- the exact vector `ApplySourceOrder` reorders on a sibling reorder, and
// what the order-insensitive canonical hash normalizes away.
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

} // namespace

// The differential oracle: a multi-edit sequence (top goal, strategy + two
// sub-goals, a solution, a text edit, a dialectic challenge, a leaf delete),
// replayed both ways, must produce byte-identical library canonical hashes.
// This proves the library seams reproduce the legacy mutators for every element
// / text / challenge / delete event, end to end, through the real command bus.
TEST(LibraryReplayConvergence, MultiEditElementSequenceConvergesWithLegacyReplay) {
    auto f = MakeFixture("multi_edit");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateTopGoalCommand top_goal;
    ASSERT_TRUE(bus->Execute(top_goal, ctx, "tester").success);

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();

    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);
    const std::string sub1_id = add_sub1.GeneratedId();

    core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub2, ctx, "tester").success);

    core::commands::CreateChildElementCommand add_solution(sub1_id, core::NewElementKind::Solution);
    ASSERT_TRUE(bus->Execute(add_solution, ctx, "tester").success);
    const std::string solution_id = add_solution.GeneratedId();

    // A name edit on G1 (maps cleanly to SetName on both paths); a content edit
    // on G1 itself -- the snapshot's `description=`-only claim, whose Content edit
    // used to diverge (the library seam overwrote the front Description; the
    // legacy mutator kept the note and wrote a separate content field). Slice
    // 3b-1 bridges the Content edit, so this now converges on the raw hash. Plus
    // a content edit on a freshly-created sub-goal (clean content semantics, no
    // pre-existing Description) -- both routed through the same bridge.
    core::commands::UpdateElementTextCommand name_edit("G1", core::ElementTextField::Name, "en", "Revised top goal");
    ASSERT_TRUE(bus->Execute(name_edit, ctx, "tester").success);
    core::commands::UpdateElementTextCommand g1_content_edit(
        "G1", core::ElementTextField::Content, "en", "The system is fully safe.");
    ASSERT_TRUE(bus->Execute(g1_content_edit, ctx, "tester").success);
    core::commands::UpdateElementTextCommand content_edit(
        sub1_id, core::ElementTextField::Content, "en", "The subsystem is acceptably safe.");
    ASSERT_TRUE(bus->Execute(content_edit, ctx, "tester").success);

    core::commands::CreateChallengeCommand challenge(core::ArgumentTarget{core::ArgumentTarget::Kind::Element, "G1"},
                                                     core::ChallengeSourceType::CounterArgument);
    ASSERT_TRUE(bus->Execute(challenge, ctx, "tester").success);

    core::commands::RemoveElementCommand remove(solution_id, core::RemoveMode::NodeAndDescendants);
    ASSERT_TRUE(bus->Execute(remove, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();

    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// The GSN repair events must replay into a library document, not just through
// the legacy mutators. They were added to `ApplyEvent` and missed in
// `ApplyEventToLibrary`, which ends in "Unknown event type" -- so any project
// where a user had repaired a reported GSN defect would have failed to replay,
// restore or verify. Nothing caught it, because no test replayed a repair.
TEST(LibraryReplayConvergence, GsnRepairEventsConvergeWithLegacyReplay) {
    auto f = MakeFixture("gsn_repair");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    // Build something to repair: a strategy over two sub-goals, plus a context.
    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();

    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);
    const std::string sub1_id = add_sub1.GeneratedId();
    const std::string inference_id = add_sub1.GeneratedRelationshipId();
    ASSERT_FALSE(inference_id.empty());

    core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub2, ctx, "tester").success);

    core::commands::CreateChildElementCommand add_context("G1", core::NewElementKind::Context);
    ASSERT_TRUE(bus->Execute(add_context, ctx, "tester").success);
    const std::string context_relationship_id = add_context.GeneratedRelationshipId();
    ASSERT_FALSE(context_relationship_id.empty());

    // Withdraw the context relationship: the context element itself survives.
    core::commands::RemoveRelationshipCommand remove_relationship(context_relationship_id);
    ASSERT_TRUE(bus->Execute(remove_relationship, ctx, "tester").success);

    // Drop one source from the strategy's inference. It keeps its reasoning and
    // its other sub-goal, so the relationship survives the scrub.
    core::commands::DropRelationshipReferenceCommand drop_reference(inference_id, sub1_id);
    ASSERT_TRUE(bus->Execute(drop_reference, ctx, "tester").success);
    EXPECT_FALSE(drop_reference.RemovedRelationship());

    core::commands::SetElementUndevelopedCommand set_undeveloped("G1", true);
    ASSERT_TRUE(bus->Execute(set_undeveloped, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();

    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// `MoveStrategyToReasoning` repairs a structure the tool cannot create, so no
// bus-built sequence reaches it. Replay it anyway and assert the failure comes
// from the mutator rather than from the dispatcher: that distinguishes "this
// event does not apply here" from "this event type is unknown", which is the
// defect the test above exists to prevent.
TEST(LibraryReplayConvergence, MoveStrategyToReasoningIsDispatchedNotRejectedAsUnknown) {
    auto f = MakeFixture("move_strategy_dispatch");

    core::audit::AuditEvent event;
    event.event_sequence = 1;
    event.event_type = "MoveStrategyToReasoning";
    event.payload = nlohmann::ordered_json::object();
    event.payload["relationship_id"] = "R-nonexistent";
    event.payload["strategy_id"] = "S-nonexistent";

    core::audit::AuditTransaction transaction;
    transaction.transaction_sequence = 1;
    transaction.events.push_back(event);

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(f.snapshot_abs);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    auto replayed = core::audit::Replayer::ReplayToLibrary(
        std::move(loaded.document), {transaction}, std::numeric_limits<std::uint64_t>::max());

    ASSERT_FALSE(replayed.has_value());
    EXPECT_EQ(replayed.error().find("Unknown event type"), std::string::npos)
        << "the repair events must be dispatched by the library replay path: " << replayed.error();
    EXPECT_NE(replayed.error().find("R-nonexistent"), std::string::npos)
        << "the failure should be the mutator's, naming what it could not find: " << replayed.error();
}

// Focused convergence for the create-child / challenge encoding in isolation:
// a strategy with two sub-goals uses the single-inference encoding, so the
// library replay must produce exactly one inference for the strategy, matching
// the legacy replay's canonical hash.
TEST(LibraryReplayConvergence, StrategyWithSubGoalsConverges) {
    auto f = MakeFixture("strategy_subgoals");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();
    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);
    core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub2, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// Terminology create + associate now converges on the RAW canonical hash. The
// gid-minting CREATE and ASSOCIATE events are bridged through the legacy
// mutators, which mint the `gid-<id>` (via GenerateUniqueGid) and force the
// ArtifactReference / AssertedContext gids that the library seams could not
// assign. Slice 3b-1 resolves impedance 1 from slice 3a -- no gid normalization
// is needed anymore.
TEST(LibraryReplayConvergence, TerminologyCreateAndAssociateConverge) {
    auto f = MakeFixture("terminology");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateTerminologyPackageCommand create_package("Terms", "Shared definitions.");
    ASSERT_TRUE(bus->Execute(create_package, ctx, "tester").success);
    const core::TerminologyPackageRef package_ref = create_package.GeneratedRef();

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    core::commands::CreateTerminologyTermCommand create_term(package_ref, draft);
    ASSERT_TRUE(bus->Execute(create_term, ctx, "tester").success);

    core::commands::AssociateTerminologyTermWithElementCommand associate("G1", package_ref, create_term.GeneratedRef());
    ASSERT_TRUE(bus->Execute(associate, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// Focused pin for impedance 2 (claim content vs second-Description slot): G1 is
// loaded from a `description=` attribute (no `content=`), so the library puts
// that text in its FRONT Description = the app's content slot. Editing G1's
// Content used to diverge -- the library seam `apply_text_edit(Content)`
// overwrote the front Description, while the legacy mutator wrote a separate
// content field and kept the original as the note. Slice 3b-1 bridges the
// Content edit through the legacy SetElementTextField, so the raw canonical
// hashes converge with no gid or slot normalization.
TEST(LibraryReplayConvergence, ContentEditOnDescriptionOnlyClaimConverges) {
    auto f = MakeFixture("content_edit_desc_only");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::UpdateElementTextCommand content_edit(
        "G1", core::ElementTextField::Content, "en", "The system is fully safe.");
    ASSERT_TRUE(bus->Execute(content_edit, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// Focused pin for the visible-context association bridge: "add as visible
// context" creates an ArtifactReference and an AssertedContext (with the visible
// marker) carrying legacy-minted gids the library seam could not assign. Slice
// 3b-1 bridges it, so create-package + create-term + add-as-visible-context
// converges on the raw canonical hash.
TEST(LibraryReplayConvergence, AddTerminologyVisibleContextBridgeConverges) {
    auto f = MakeFixture("terminology_visible_context");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateTerminologyPackageCommand create_package("Terms", "Shared definitions.");
    ASSERT_TRUE(bus->Execute(create_package, ctx, "tester").success);
    const core::TerminologyPackageRef package_ref = create_package.GeneratedRef();

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    core::commands::CreateTerminologyTermCommand create_term(package_ref, draft);
    ASSERT_TRUE(bus->Execute(create_term, ctx, "tester").success);

    core::commands::AddTerminologyTermAsVisibleContextCommand add_visible(
        "G1", package_ref, create_term.GeneratedRef());
    ASSERT_TRUE(bus->Execute(add_visible, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// Removing ONE sub-goal of a strategy whose single inference has SEVERAL sources
// must scrub that source and KEEP the inference (the legacy rule), not cascade the
// whole relationship away -- cascading would silently detach the strategy and its
// remaining sub-goal from the argument. This is the shape the other delete test
// does not cover (its target is a single-source leaf).
TEST(LibraryReplayConvergence, RemoveOneSourceOfSharedStrategyInferenceConverges) {
    auto f = MakeFixture("shared_inference_remove");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();
    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);
    core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub2, ctx, "tester").success);

    // Remove the first sub-goal; the strategy's single inference still has the
    // second sub-goal as a source, so it must survive with the source scrubbed.
    core::commands::RemoveElementCommand remove(add_sub1.GeneratedId(), core::RemoveMode::NodeAndDescendants);
    ASSERT_TRUE(bus->Execute(remove, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// NodeOnly removal of a non-strategy INTERIOR goal REPARENTS its child onto the
// grandparent: legacy core::RemoveElement RETARGETS the child's inference from the
// removed node to the parent (ReparentChildrenToParent). That retarget is NOT a
// delete, so replaying `deleted_ids` alone through the scrub seam would scrub the
// removed node out of the child's inference target, leave it target-less, drop it,
// and orphan the grandchild -- diverging from legacy. NodeOnly must therefore
// bridge through the legacy mutator on replay. This pins it: G1 -> E -> C, remove
// E NodeOnly, C promoted under G1 on BOTH paths. (NodeAndDescendants, covered by
// the tests above, has no reparenting and stays on the seam.)
TEST(LibraryReplayConvergence, RemoveNodeOnlyInteriorReparentsAndConverges) {
    auto f = MakeFixture("node_only_interior");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    // A plain goal-under-goal chain (no strategy): each link is an assertedinference
    // {target=parent, source=child}, so removing the middle goal exercises the
    // target-retarget path, not the reasoning-clear path a strategy would take.
    core::commands::CreateChildElementCommand add_e("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_e, ctx, "tester").success);
    const std::string e_id = add_e.GeneratedId();
    core::commands::CreateChildElementCommand add_c(e_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_c, ctx, "tester").success);

    core::commands::RemoveElementCommand remove(e_id, core::RemoveMode::NodeOnly);
    ASSERT_TRUE(bus->Execute(remove, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// ACP CRUD (Phase 2 slice 2c-1) is library-primary via the bridge: AddAcp forces
// the recorded ACP<n> id via core::acp::AddAcpWithId, and Upsert re-runs the same
// legacy mutator on the projected package. Create a Solution (the only element
// kind ElementEligibleForAcp accepts), add an element ACP on it, then edit its
// name -- and the raw canonical hashes must converge across both replays.
TEST(LibraryReplayConvergence, AcpAddAndUpsertConverge) {
    auto f = MakeFixture("acp_add_upsert");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
    ASSERT_TRUE(bus->Execute(add_solution, ctx, "tester").success);
    const std::string solution_id = add_solution.GeneratedId();

    core::commands::AddAcpCommand add_acp("element", solution_id);
    ASSERT_TRUE(bus->Execute(add_acp, ctx, "tester").success);
    const std::string acp_id = add_acp.GeneratedAcpId();
    ASSERT_FALSE(acp_id.empty());

    parser::AcpRecord edited;
    edited.id = acp_id;
    edited.name = "Confidence in the test report";
    edited.target_kind = "element";
    edited.target_id = solution_id;
    edited.resolution_kind = "none";
    core::commands::UpsertAcpCommand upsert(edited);
    ASSERT_TRUE(bus->Execute(upsert, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// The RemoveAcp event replays through the same bridge (the legacy core::acp::RemoveAcp
// strips the vendor ACP TaggedValues). Add then remove an element ACP -- both replays
// must land on the ACP-free canonical hash.
TEST(LibraryReplayConvergence, AcpRemoveConverge) {
    auto f = MakeFixture("acp_remove");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
    ASSERT_TRUE(bus->Execute(add_solution, ctx, "tester").success);
    const std::string solution_id = add_solution.GeneratedId();

    core::commands::AddAcpCommand add_acp("element", solution_id);
    ASSERT_TRUE(bus->Execute(add_acp, ctx, "tester").success);
    const std::string acp_id = add_acp.GeneratedAcpId();
    ASSERT_FALSE(acp_id.empty());

    core::commands::RemoveAcpCommand remove_acp(acp_id);
    ASSERT_TRUE(bus->Execute(remove_acp, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// Creating a confidence argument tree for an ACP is a COMPOUND op (a confidence
// ArgumentPackage + top goal + linking UpsertAcp) that mints two ids. The command
// records both; the bridged replay forces them via
// CreateConfidenceArgumentTreeForAcpWithIds, so legacy and library replay converge.
TEST(LibraryReplayConvergence, AcpCreateConfidenceTreeConverges) {
    auto f = MakeFixture("acp_confidence_tree");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
    ASSERT_TRUE(bus->Execute(add_solution, ctx, "tester").success);
    const std::string solution_id = add_solution.GeneratedId();

    core::commands::AddAcpCommand add_acp("element", solution_id);
    ASSERT_TRUE(bus->Execute(add_acp, ctx, "tester").success);
    const std::string acp_id = add_acp.GeneratedAcpId();
    ASSERT_FALSE(acp_id.empty());

    core::commands::CreateConfidenceArgumentTreeForAcpCommand create_tree(acp_id);
    ASSERT_TRUE(bus->Execute(create_tree, ctx, "tester").success);
    ASSERT_FALSE(create_tree.GeneratedArgumentPackageId().empty());
    ASSERT_FALSE(create_tree.GeneratedTopGoalId().empty());

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// A sibling REORDER changes only the ORDER of a relationship's sources, which the
// order-insensitive canonical hash (it sorts sources and relationships) normalizes
// away -- so the canonical hash is NOT a witness that the reorder happened. Prove
// non-vacuousness against the order-sensitive RAW serialization (the file the app
// saves), then prove the bridged library replay reproduces the legacy replay's
// source order exactly -- i.e. the reorder survives the library round-trip, which
// is precisely the data loss the audit fix must prevent.
TEST(LibraryReplayConvergence, TreeReorderSiblingsConvergesAndChangesSerialization) {
    auto f = MakeFixture("tree_reorder");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();
    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);
    const std::string sub1_id = add_sub1.GeneratedId();
    core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub2, ctx, "tester").success);
    const std::string sub2_id = add_sub2.GeneratedId();

    // The strategy's single inference sources the two sub-goals in creation order.
    ASSERT_EQ(StrategyInferenceSources(f.package, strategy_id), (std::vector<std::string>{sub1_id, sub2_id}));
    const std::string serialized_before = sacm::serialize_sacm(f.package);

    // Move sub2 ABOVE sub1.
    core::commands::ReorderSiblingsCommand reorder(sub2_id, sub1_id, core::TreeDropMode::Before);
    ASSERT_TRUE(bus->Execute(reorder, ctx, "tester").success);

    // Non-vacuous: the reorder changed the serialized SACM (the raw file the app
    // saves), even though the order-insensitive canonical hash cannot see it.
    const std::string serialized_after = sacm::serialize_sacm(f.package);
    EXPECT_NE(serialized_before, serialized_after);
    EXPECT_EQ(StrategyInferenceSources(f.package, strategy_id), (std::vector<std::string>{sub2_id, sub1_id}));

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);
    const sacm::AssuranceCasePackage library_package = core::project_library_package(*library_doc);

    // Order-sensitive convergence: the bridged library replay reproduces the legacy
    // replay's reordered source order (the reorder survived the library round-trip),
    // and it matches the intended reorder.
    const std::vector<std::string> legacy_sources = StrategyInferenceSources(legacy.package, strategy_id);
    const std::vector<std::string> library_sources = StrategyInferenceSources(library_package, strategy_id);
    EXPECT_EQ(legacy_sources, (std::vector<std::string>{sub2_id, sub1_id}));
    EXPECT_EQ(library_sources, legacy_sources);

    // The canonical hash still matches (order-insensitively) -- the standard oracle.
    const std::optional<std::string> library_hash = core::library_canonical_hash(library_package);
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// MoveSubtree changes the relationship structure (a source moves under a new
// parent, the old relationship is dropped, a new one minted), so it DOES change
// the canonical hash -- the standard oracle applies directly. The new relationship
// id is minted deterministically (GenerateRelationshipId), so the bridged library
// replay regenerates the same id the legacy replay does and they converge without a
// WithId variant.
TEST(LibraryReplayConvergence, TreeMoveSubtreeConvergesWithLegacyReplay) {
    auto f = MakeFixture("tree_move");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_a("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_a, ctx, "tester").success);
    const std::string a_id = add_a.GeneratedId();
    core::commands::CreateChildElementCommand add_b("G1", core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_b, ctx, "tester").success);
    const std::string b_id = add_b.GeneratedId();

    const std::optional<std::string> hash_before = core::library_canonical_hash(f.package);
    ASSERT_TRUE(hash_before.has_value());

    // Move Gb from under G1 to under Ga.
    core::commands::MoveSubtreeCommand move(b_id, a_id);
    ASSERT_TRUE(bus->Execute(move, ctx, "tester").success);

    // Non-vacuous: the move changed the canonical hash (unlike a pure reorder).
    const std::optional<std::string> hash_after = core::library_canonical_hash(f.package);
    ASSERT_TRUE(hash_after.has_value());
    EXPECT_NE(*hash_before, *hash_after);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// A SetElementGid event assigns a random gid to an element that lacks one -- the
// gid the confidence-save flow used to write inline, un-audited. Element gids ARE
// in the canonical hash, so the assignment must be recorded and replay-stable. The
// live command mints the gid ONCE and records it; both replays force that value
// (legacy via core::SetElementGid, library via the bridge), so they converge. The
// hash BEFORE vs AFTER the assignment must DIFFER (else the test is vacuous -- gids
// contribute to the hash, and an un-audited gid write is exactly the divergence
// this audits away).
TEST(LibraryReplayConvergence, SetElementGidConvergesAndChangesCanonicalHash) {
    auto f = MakeFixture("set_element_gid");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    // A freshly created solution carries no gid: the element factory mints ids but
    // no gid, which is exactly the gap the confidence-save flow filled inline.
    core::commands::CreateChildElementCommand add_solution("G1", core::NewElementKind::Solution);
    ASSERT_TRUE(bus->Execute(add_solution, ctx, "tester").success);
    const std::string solution_id = add_solution.GeneratedId();
    const parser::SacmElement* created = parser::FindElementById(f.model, solution_id);
    ASSERT_NE(created, nullptr);
    ASSERT_TRUE(created->gid.empty()) << "a freshly created element unexpectedly already has a gid";

    // The canonical hash before the gid assignment (gids are in the hash).
    const std::optional<std::string> hash_before = core::library_canonical_hash(f.package);
    ASSERT_TRUE(hash_before.has_value());

    core::commands::EnsureElementGidCommand assign_gid(solution_id);
    ASSERT_TRUE(bus->Execute(assign_gid, ctx, "tester").success);
    ASSERT_FALSE(assign_gid.GeneratedGid().empty());

    // Non-vacuous: assigning the gid moved the canonical hash off its prior value.
    const std::optional<std::string> hash_after = core::library_canonical_hash(f.package);
    ASSERT_TRUE(hash_after.has_value());
    EXPECT_NE(*hash_before, *hash_after);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// Bridge event: RemoveTerminologyPackage has no parity seam (the library's
// recursive delete diverges from the legacy non-empty-refusing mutator), so the
// library replay bridges -- applying the legacy mutator onto a projected package
// and re-deriving the library. Creating then removing an (empty) terminology
// package must leave the library replay converged with the legacy replay.
// Renamed from ...BridgeConverges in phase 2a: this event no longer bridges on
// either side, so the old name asserted something untrue about what it measures.
TEST(LibraryReplayConvergence, RemoveTerminologyPackageConverges) {
    auto f = MakeFixture("bridge_remove_terminology");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateTerminologyPackageCommand create_package("Scratch", "");
    ASSERT_TRUE(bus->Execute(create_package, ctx, "tester").success);
    const core::TerminologyPackageRef package_ref = create_package.GeneratedRef();

    core::commands::RemoveTerminologyPackageCommand remove_package(package_ref.id, package_ref.gid);
    ASSERT_TRUE(bus->Execute(remove_package, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
}

// Phase 2a: RemoveArtifactPackage is seam-mapped on both sides now, so this is a
// genuine differential rather than the bridge agreeing with itself.
//
// The package here is EMPTY, deliberately. Removing one whose artifacts an
// ArtifactReference cites is where the seam and the legacy mutator part company
// (the seam scrubs the reference, the legacy mutator leaves it dangling), so the
// legacy oracle cannot certify that case by construction -- it is measured
// directly, live-against-legacy, by
// LibraryPrimaryEditFlip.RemoveArtifactPackageScrubsTheReferenceTheLegacyMutatorLeftDangling,
// and its live-against-replay agreement by the VerifyProject in that same test.
TEST(LibraryReplayConvergence, RemoveArtifactPackageConverges) {
    constexpr const char* kEmptyArtifactPackageSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <artifactPackage id="ARTP1" name="Evidence"/>
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    auto f = MakeFixture("remove_artifact_package", kEmptyArtifactPackageSacm);

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::RemoveArtifactPackageCommand remove("ARTP1", "");
    ASSERT_TRUE(bus->Execute(remove, ctx, "tester").success);

    const std::vector<core::audit::AuditTransaction> txns = bus->Store().Transactions();
    const core::audit::ReplayState legacy = LegacyReplay(f, txns);
    const std::unique_ptr<sacm_adapter::LibraryDocument> library_doc = LibraryReplay(f, txns);
    ASSERT_NE(library_doc, nullptr);

    const std::optional<std::string> library_hash =
        core::library_canonical_hash(core::project_library_package(*library_doc));
    const std::optional<std::string> legacy_hash = core::library_canonical_hash(legacy.package);
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_hash.has_value());
    EXPECT_EQ(*library_hash, *legacy_hash);
    EXPECT_TRUE(core::project_library_package(*library_doc).artifactPackages.empty())
        << "the replay did not remove the artifact package at all";
}
