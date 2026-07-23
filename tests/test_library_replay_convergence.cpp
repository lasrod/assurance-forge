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
// absent on the other -- which is exactly how the terminology gid impedance
// (documented in src/sacm_adapter/document_edit.h) surfaces below.
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

void blank_element_gid(sacm::SacmElement& element) { element.gid.clear(); }

void blank_terminology_package_gids(sacm::TerminologyPackage& tp) {
    blank_element_gid(tp);
    for (sacm::Category& c : tp.categories) blank_element_gid(c);
    for (sacm::Expression& e : tp.expressions) blank_element_gid(e);
    for (sacm::Term& t : tp.terms) blank_element_gid(t);
}

// Clears every gid in the package. Used to prove that the ONLY residual
// divergence between the library-created terminology and the legacy-created
// terminology is the gid the library seam cannot assign (clause 8.2 gid is
// optional). Once gid is normalized away, StableKey falls back to id -- which
// the library replay forces to match the legacy id -- so the two converge.
sacm::AssuranceCasePackage BlankAllGids(sacm::AssuranceCasePackage package) {
    blank_element_gid(package);
    for (sacm::TerminologyPackage& tp : package.terminologyPackages) blank_terminology_package_gids(tp);
    for (sacm::ArtifactPackage& ap : package.artifactPackages) {
        blank_element_gid(ap);
        for (sacm::Artifact& a : ap.artifacts) blank_element_gid(a);
    }
    for (sacm::ArgumentPackage& ap : package.argumentPackages) {
        blank_element_gid(ap);
        for (sacm::TerminologyPackage& tp : ap.terminologyPackages) blank_terminology_package_gids(tp);
        for (sacm::Claim& c : ap.claims) blank_element_gid(c);
        for (sacm::ArgumentReasoning& r : ap.argumentReasonings) blank_element_gid(r);
        for (sacm::ArtifactReference& r : ap.artifactReferences) blank_element_gid(r);
        for (sacm::AssertedInference& r : ap.assertedInferences) blank_element_gid(r);
        for (sacm::AssertedContext& r : ap.assertedContexts) blank_element_gid(r);
        for (sacm::AssertedEvidence& r : ap.assertedEvidences) blank_element_gid(r);
    }
    return package;
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

    // A name edit on G1 (maps cleanly to SetName on both paths) and a content
    // edit on a freshly-created sub-goal (which has no pre-existing Description
    // to collide with, unlike G1's `description=` attribute -- see the claim
    // content/description impedance in src/sacm_adapter/document_edit.h).
    core::commands::UpdateElementTextCommand name_edit("G1", core::ElementTextField::Name, "en",
                                                       "Revised top goal");
    ASSERT_TRUE(bus->Execute(name_edit, ctx, "tester").success);
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

// Terminology create + associate: the library replay converges with the legacy
// replay on CONTENT, but NOT on the raw canonical hash -- the library seams
// cannot assign the `gid-<id>` the legacy mutators generate (clause 8.2 gid is
// optional; the library has no create-time gid operation). This test pins that
// impedance precisely: content parity once gid is normalized away, plus a raw
// hash divergence proving gid is the *only* residual difference. It does NOT
// weaken the differential oracle -- it documents a real seam divergence, the
// resolution of which is a later flip-slice concern.
TEST(LibraryReplayConvergence, TerminologyCreateAndAssociateConvergeModuloGid) {
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
    const std::optional<std::string> legacy_raw_hash = core::library_canonical_hash(legacy.package);
    const std::optional<std::string> legacy_blanked_hash =
        core::library_canonical_hash(BlankAllGids(legacy.package));
    ASSERT_TRUE(library_hash.has_value());
    ASSERT_TRUE(legacy_raw_hash.has_value());
    ASSERT_TRUE(legacy_blanked_hash.has_value());

    // Content converges once the gid the seam cannot assign is normalized away.
    EXPECT_EQ(*library_hash, *legacy_blanked_hash)
        << "terminology content diverged beyond gid -- a structural seam problem, not the gid impedance";
    // ... but the raw hashes differ, confirming gid is the sole residual divergence.
    EXPECT_NE(*library_hash, *legacy_raw_hash)
        << "expected the terminology gid impedance to make the raw hashes differ";
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
