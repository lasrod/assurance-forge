#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_store.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_replayer.h"
#include "core/audit/event_store.h"
#include "core/audit/replay_verifier.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>

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
                ("af_replay_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
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
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
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
    auto pkg = sacm::parse_sacm(f.sacm_abs.string());
    EXPECT_TRUE(pkg.has_value()) << (pkg.has_value() ? "" : pkg.error());
    f.package = std::move(pkg.value());
    auto parsed = parser::parse_sacm_xml_string(kSampleSacm);
    EXPECT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error());
    f.model = std::move(parsed.value());
    return f;
}

// Load the initial snapshot state from disk into a fresh ReplayState the
// replayer can extend.
core::audit::ReplayState LoadSnapshotState(const std::filesystem::path& root, const std::string& snapshot_id) {
    core::audit::ReplayState state;
    const auto sacm_path = core::audit::SnapshotSacmPath(root, snapshot_id);
    auto pkg = sacm::parse_sacm(sacm_path.string());
    EXPECT_TRUE(pkg.has_value());
    auto ac = parser::parse_sacm_xml(sacm_path.string());
    EXPECT_TRUE(ac.has_value());
    state.package = std::move(*pkg);
    state.model = std::move(*ac);
    return state;
}

} // namespace

TEST(EventReplayer, ReplaysSingleCreateChildToMatchLiveState) {
    auto f = MakeFixture("single_create");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Strategy);
    core::commands::CommandContext ctx{f.model, f.package};
    auto live_result = bus->Execute(cmd, ctx, "tester");
    ASSERT_TRUE(live_result.success) << live_result.error;

    // Replay from snapshot zero.
    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, bus->Store().Transactions(), std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());

    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package), core::audit::CanonicalModelHash(f.package));

    // The replayed element keeps the id captured in the event payload.
    bool found = false;
    for (const auto& e : replayed->model.elements) {
        if (e.id == cmd.GeneratedId() && e.type == "argumentreasoning") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "replayed strategy " << cmd.GeneratedId() << " missing";
}

namespace {

int CountStrategyInferences(const sacm::AssuranceCasePackage& package, const std::string& strategy_id) {
    int count = 0;
    for (const auto& ap : package.argumentPackages)
        for (const auto& inference : ap.assertedInferences)
            if (inference.reasoning == strategy_id)
                ++count;
    return count;
}

} // namespace

// A strategy with multiple sub-goals uses the single-inference encoding on BOTH
// the live edit and replay -- add-strategy records no relationship, the first
// sub-goal materializes the inference, later ones extend it -- so the replayed
// canonical hash matches the live one. This is the audit-integrity guarantee for
// the encoding change: without it, library-primary replay would diverge on any
// case containing a strategy. Phase 9 Stage 7 (SACM23-INT-001).
TEST(EventReplayer, ReplaysStrategyWithSubGoalsMatchesCanonicalHash) {
    auto f = MakeFixture("strategy_subgoals");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();
    EXPECT_TRUE(add_strategy.GeneratedRelationshipId().empty()) << "add-strategy records no relationship";

    core::commands::CreateChildElementCommand add_sub1(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub1, ctx, "tester").success);
    EXPECT_FALSE(add_sub1.GeneratedRelationshipId().empty()) << "first sub-goal materializes (records) the inference";

    core::commands::CreateChildElementCommand add_sub2(strategy_id, core::NewElementKind::Goal);
    ASSERT_TRUE(bus->Execute(add_sub2, ctx, "tester").success);
    EXPECT_TRUE(add_sub2.GeneratedRelationshipId().empty()) << "later sub-goal extends; records no relationship";

    // The live edit produced exactly one inference for the strategy.
    EXPECT_EQ(CountStrategyInferences(f.package, strategy_id), 1);

    // Replay from snapshot zero reconstructs the same package by construction
    // (record and replay both route through the single-inference factory).
    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, bus->Store().Transactions(), std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());

    EXPECT_EQ(CountStrategyInferences(replayed->package, strategy_id), 1)
        << "replay must reproduce the single-inference encoding";

    // Both the legacy canonical hash and the library-derived hash (the one the
    // audit chain actually records) converge across live and replay.
    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package), core::audit::CanonicalModelHash(f.package));
    const auto live_library_hash = core::library_canonical_hash(f.package);
    const auto replayed_library_hash = core::library_canonical_hash(replayed->package);
    ASSERT_TRUE(live_library_hash.has_value());
    ASSERT_TRUE(replayed_library_hash.has_value());
    EXPECT_EQ(*replayed_library_hash, *live_library_hash);
}

TEST(EventReplayer, ReplaysCreateChainThenRemoveAndMatchesCanonicalHash) {
    auto f = MakeFixture("create_then_remove");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add_strategy("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add_strategy, ctx, "tester").success);
    const std::string strategy_id = add_strategy.GeneratedId();

    core::commands::CreateChildElementCommand add_solution(strategy_id, core::NewElementKind::Solution);
    ASSERT_TRUE(bus->Execute(add_solution, ctx, "tester").success);
    const std::string solution_id = add_solution.GeneratedId();

    core::commands::RemoveElementCommand remove(solution_id, core::RemoveMode::NodeOnly);
    ASSERT_TRUE(bus->Execute(remove, ctx, "tester").success);

    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, bus->Store().Transactions(), std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());

    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package), core::audit::CanonicalModelHash(f.package));

    // Strategy survived (still in the model); solution was removed.
    bool has_strategy = false, has_solution = false;
    for (const auto& e : replayed->model.elements) {
        if (e.id == strategy_id)
            has_strategy = true;
        if (e.id == solution_id)
            has_solution = true;
    }
    EXPECT_TRUE(has_strategy);
    EXPECT_FALSE(has_solution);
}

TEST(EventReplayer, StopsAtRequestedTransactionSequence) {
    auto f = MakeFixture("partial_replay");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand a("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(a, ctx, "tester").success);
    core::commands::CreateChildElementCommand b("G1", core::NewElementKind::Context);
    ASSERT_TRUE(bus->Execute(b, ctx, "tester").success);

    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed_only_first =
        core::audit::Replayer::ReplayFrom(snapshot.model, snapshot.package, bus->Store().Transactions(), /*up_to=*/1);
    ASSERT_TRUE(replayed_only_first.has_value());

    bool has_a = false, has_b = false;
    for (const auto& e : replayed_only_first->model.elements) {
        if (e.id == a.GeneratedId())
            has_a = true;
        if (e.id == b.GeneratedId())
            has_b = true;
    }
    EXPECT_TRUE(has_a);
    EXPECT_FALSE(has_b);
}

TEST(EventReplayer, ReturnsErrorOnUnknownEventType) {
    auto f = MakeFixture("unknown_event");

    core::audit::AuditTransaction tx;
    tx.transaction_sequence = 1;
    tx.command_name = "Bogus";
    core::audit::AuditEvent ev;
    ev.event_sequence = 1;
    ev.event_type = "Nonexistent";
    ev.payload = nlohmann::ordered_json::object();
    tx.events.push_back(ev);

    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, {tx}, std::numeric_limits<std::uint64_t>::max());
    ASSERT_FALSE(replayed.has_value());
    EXPECT_NE(replayed.error().find("Nonexistent"), std::string::npos);
    EXPECT_NE(replayed.error().find("transaction 1"), std::string::npos);
}

TEST(ReplayVerifier, ReportsSuccessForCleanProject) {
    auto f = MakeFixture("verifier_ok");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(cmd, ctx, "tester").success);

    const auto result = core::audit::VerifyProject(f.project);
    EXPECT_TRUE(result.ran);
    EXPECT_TRUE(result.success) << [&] {
        std::string s;
        for (const auto& d : result.diagnostics)
            s += "\n  - " + d;
        return s;
    }();
    EXPECT_EQ(result.replayed_canonical_hash, result.manifest_canonical_hash);
}

TEST(ReplayVerifier, RebuildsStaleManifestSilentlyWhenReplayMatchesOnDiskSacm) {
    auto f = MakeFixture("verifier_manifest_stale");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(cmd, ctx, "tester").success);

    // Stale manifest hash — log and on-disk SACM still agree, only the
    // cached manifest is wrong. Phase 0: verifier rewrites it silently.
    core::audit::AuditManifest manifest;
    ASSERT_TRUE(core::audit::ReadAuditManifest(f.project.rootPath, manifest, error));
    const std::string stale_hash(64, '0');
    manifest.last_known_canonical_model_hash = stale_hash;
    manifest.last_known_raw_file_hash = stale_hash;
    ASSERT_TRUE(core::audit::WriteAuditManifest(f.project.rootPath, manifest, error));

    const auto result = core::audit::VerifyProject(f.project);
    EXPECT_TRUE(result.ran);
    EXPECT_TRUE(result.success) << [&] {
        std::string s;
        for (const auto& d : result.diagnostics)
            s += "\n  - " + d;
        return s;
    }();
    EXPECT_EQ(result.manifest_canonical_hash, result.replayed_canonical_hash);

    // Manifest on disk should now reflect the rebuilt hashes.
    core::audit::AuditManifest after;
    ASSERT_TRUE(core::audit::ReadAuditManifest(f.project.rootPath, after, error));
    EXPECT_EQ(after.last_known_canonical_model_hash, result.replayed_canonical_hash);
    EXPECT_NE(after.last_known_canonical_model_hash, stale_hash);
    EXPECT_NE(after.last_known_raw_file_hash, stale_hash);
}

TEST(ReplayVerifier, ReportsMismatchWhenOnDiskSacmDivergesFromLog) {
    auto f = MakeFixture("verifier_disk_diverged");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(cmd, ctx, "tester").success);

    // Externally mutate the materialized SACM so it no longer matches the
    // replayed state. This is the real divergence we must still flag.
    static constexpr const char* kTampered = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="EXTERNALLY EDITED."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";
    WriteFile(f.sacm_abs, kTampered);

    const auto result = core::audit::VerifyProject(f.project);
    EXPECT_TRUE(result.ran);
    EXPECT_FALSE(result.success);
    bool mentions_on_disk = false;
    for (const auto& d : result.diagnostics) {
        if (d.find("on-disk") != std::string::npos)
            mentions_on_disk = true;
    }
    EXPECT_TRUE(mentions_on_disk);
}

TEST(ReplayVerifier, SkipsProjectWithoutAuditStore) {
    core::AssuranceProject project;
    project.id = "x";
    project.name = "no audit";
    project.rootPath = MakeTempProjectRoot("verifier_skip");
    const auto result = core::audit::VerifyProject(project);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.ran);
}

TEST(EventReplayer, ReplaysUpdateElementTextToMatchLiveState) {
    auto f = MakeFixture("update_text");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::UpdateElementTextCommand edit("G1", core::ElementTextField::Description, "en", "Updated by test.");
    const auto live_result = bus->Execute(edit, ctx, "tester");
    ASSERT_TRUE(live_result.success) << live_result.error;
    EXPECT_EQ(edit.OldValue(), "The system is safe.");
    EXPECT_FALSE(edit.WasNoOp());

    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, bus->Store().Transactions(), std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());
    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package), core::audit::CanonicalModelHash(f.package));

    bool found = false;
    for (const auto& e : replayed->model.elements) {
        if (e.id == "G1") {
            EXPECT_EQ(e.description, "Updated by test.");
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(EventReplayer, GSN3_CORE_010_ReplaysIndependentNotationIdentifierEdit) {
    auto f = MakeFixture("update_gsn_identifier");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::UpdateGsnIdentifierCommand edit("G1", "SYS-GOAL");
    const auto live_result = bus->Execute(edit, ctx, "tester");
    ASSERT_TRUE(live_result.success) << live_result.error;
    EXPECT_EQ(edit.OldIdentifier(), "G1");
    ASSERT_EQ(bus->Store().Transactions().size(), 1u);
    EXPECT_EQ(bus->Store().Transactions().front().events.front().event_type, "UpdateGsnIdentifier");

    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, bus->Store().Transactions(), std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());
    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package), core::audit::CanonicalModelHash(f.package));
    ASSERT_EQ(replayed->model.elements.size(), 1u);
    EXPECT_EQ(replayed->model.elements.front().id, "G1");
    EXPECT_EQ(replayed->model.elements.front().gsn_identifier, "SYS-GOAL");
}

TEST(EventReplayer, ReplaysUpdateElementTextSecondaryLanguage) {
    auto f = MakeFixture("update_text_lang");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::UpdateElementTextCommand edit(
        "G1", core::ElementTextField::Name, "ja", "\xe3\x83\x88\xe3\x83\x83\xe3\x83\x97");
    ASSERT_TRUE(bus->Execute(edit, ctx, "tester").success);

    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, bus->Store().Transactions(), std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());
    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package), core::audit::CanonicalModelHash(f.package));

    for (const auto& e : replayed->model.elements) {
        if (e.id == "G1") {
            EXPECT_EQ(e.name, "Top goal");
            auto it = e.name_langs.find("ja");
            ASSERT_NE(it, e.name_langs.end());
            EXPECT_EQ(it->second, "\xe3\x83\x88\xe3\x83\x83\xe3\x83\x97");
        }
    }
}

TEST(UpdateElementTextCommand, NoOpWhenValueUnchangedStillAppendsAuditEvent) {
    auto f = MakeFixture("update_text_noop");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::UpdateElementTextCommand edit(
        "G1", core::ElementTextField::Description, "en", "The system is safe.");
    const auto result = bus->Execute(edit, ctx, "tester");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(edit.WasNoOp());
    EXPECT_EQ(edit.OldValue(), "The system is safe.");
    EXPECT_EQ(result.transaction_sequence, 1u);
}

TEST(EventReplayer, ReplaysTerminologyCommandChainToMatchLiveState) {
    // Intent: the replayer reproduces terminology mutations (package/term/
    // association events), so replaying the log yields the same SACM state as
    // the live edits. This exercises the replayer's terminology event handlers.
    auto f = MakeFixture("replay_terminology");

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

    auto snapshot = LoadSnapshotState(f.project.rootPath, core::audit::kInitialSnapshotId);
    auto replayed = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, bus->Store().Transactions(), std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());

    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package), core::audit::CanonicalModelHash(f.package));
}

TEST(UpdateElementTextCommand, RejectsContentFieldOnNonClaimElement) {
    auto f = MakeFixture("update_text_bad_content");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};

    // Add a context element, then try to set its content — should fail.
    core::commands::CreateChildElementCommand add_ctx("G1", core::NewElementKind::Context);
    ASSERT_TRUE(bus->Execute(add_ctx, ctx, "tester").success);
    const std::string context_id = add_ctx.GeneratedId();

    core::commands::UpdateElementTextCommand bad(context_id, core::ElementTextField::Content, "en", "should fail");
    const auto result = bus->Execute(bad, ctx, "tester");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("content"), std::string::npos);
}
