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
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/commands/package_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"
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
                ("af_libreplay_" + tag + "_" +
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

struct ProjectFixture {
    core::AssuranceProject     project;
    std::filesystem::path      sacm_abs;
    std::filesystem::path      snapshot_abs;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase      model;
};

ProjectFixture MakeFixture(const std::string& tag) {
    ProjectFixture f;
    const auto root = MakeTempProjectRoot(tag);
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
    f.snapshot_abs = core::audit::SnapshotSacmPath(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto pkg = sacm::parse_sacm(f.sacm_abs.string());
    EXPECT_TRUE(pkg.has_value()) << (pkg.has_value() ? "" : pkg.error());
    if (pkg.has_value())
        f.package = std::move(pkg.value());
    auto parsed = parser::parse_sacm_xml_string(kSampleSacm);
    EXPECT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error());
    if (parsed.has_value())
        f.model = std::move(parsed.value());
    return f;
}

// Legacy replay of the whole log from snapshot zero.
core::audit::ReplayState LegacyReplay(const ProjectFixture& f,
                                      const std::vector<core::audit::AuditTransaction>& txns) {
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
std::unique_ptr<sacm_adapter::LibraryDocument> LibraryReplay(
    const ProjectFixture& f, const std::vector<core::audit::AuditTransaction>& txns) {
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
    core::commands::UpdateElementTextCommand name_edit("G1", core::ElementTextField::Name, "en",
                                                       "Revised top goal");
    ASSERT_TRUE(bus->Execute(name_edit, ctx, "tester").success);
    core::commands::UpdateElementTextCommand g1_content_edit("G1", core::ElementTextField::Content,
                                                             "en", "The system is fully safe.");
    ASSERT_TRUE(bus->Execute(g1_content_edit, ctx, "tester").success);
    core::commands::UpdateElementTextCommand content_edit(sub1_id, core::ElementTextField::Content,
                                                          "en", "The subsystem is acceptably safe.");
    ASSERT_TRUE(bus->Execute(content_edit, ctx, "tester").success);

    core::commands::CreateChallengeCommand challenge(
        core::ArgumentTarget{core::ArgumentTarget::Kind::Element, "G1"},
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

    core::commands::AssociateTerminologyTermWithElementCommand associate("G1", package_ref,
                                                                         create_term.GeneratedRef());
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

    core::commands::UpdateElementTextCommand content_edit("G1", core::ElementTextField::Content,
                                                          "en", "The system is fully safe.");
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

    core::commands::AddTerminologyTermAsVisibleContextCommand add_visible("G1", package_ref,
                                                                          create_term.GeneratedRef());
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
    core::commands::RemoveElementCommand remove(add_sub1.GeneratedId(),
                                                core::RemoveMode::NodeAndDescendants);
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

// Bridge event: RemoveTerminologyPackage has no parity seam (the library's
// recursive delete diverges from the legacy non-empty-refusing mutator), so the
// library replay bridges -- applying the legacy mutator onto a projected package
// and re-deriving the library. Creating then removing an (empty) terminology
// package must leave the library replay converged with the legacy replay.
TEST(LibraryReplayConvergence, RemoveTerminologyPackageBridgeConverges) {
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
