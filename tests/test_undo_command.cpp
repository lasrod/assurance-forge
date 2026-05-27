// Phase 5 — Undo command + replayer-skip + boundary tests.
//
// These tests verify:
//  1. UndoLastTransactionCommand wholesale-replaces model/package and
//     appends a single "Undo" audit event.
//  2. Two successive undos each produce a distinct transaction and
//     correctly walk effective state backward.
//  3. Replaying with Undo markers reproduces the live post-undo state.
//  4. FindUndoBoundary and CanUndo respect snapshot and baseline walls.
//  5. FindUndoTarget skips pure-Undo transactions and resolves
//     transitively (undo-of-undo = redo).
#include "core/audit/audit_baseline.h"
#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_store.h"
#include "core/audit/audit_transaction.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_replayer.h"
#include "core/audit/event_store.h"
#include "core/audit/history_reconstruction.h"
#include "core/audit/undo_boundary.h"
#include "core/audit/undo_resolver.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/commands/undo_command.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

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
                ("af_undo_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
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
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase      model;
};

ProjectFixture MakeFixture(const std::string& tag) {
    ProjectFixture f;
    const auto root = MakeTempProjectRoot(tag);
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);

    f.project.id       = "p";
    f.project.name     = "Project";
    f.project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id           = "f1";
    entry.relativePath = sacm_rel;
    entry.role         = core::ProjectFileRole::SacmArgument;
    f.project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string                         error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(f.project, sacm_rel, ensure, error)) << error;

    f.sacm_abs  = f.project.rootPath / sacm_rel;
    auto pkg    = sacm::parse_sacm(f.sacm_abs.string());
    EXPECT_TRUE(pkg.has_value());
    f.package   = std::move(pkg.value());
    auto parsed = parser::parse_sacm_xml_string(kSampleSacm);
    EXPECT_TRUE(parsed.has_value());
    f.model     = std::move(parsed.value());
    return f;
}

// Execute Undo on the given bus, mirroring the orchestration in
// `AppRuntime::Undo()` but without the AppRuntime / UI dependencies.
bool DoUndo(core::commands::CommandBus& bus, core::commands::CommandContext& ctx,
            const core::AssuranceProject& project, std::string& error_out) {
    const auto& transactions = bus.Store().Transactions();
    const auto  target       = core::audit::FindUndoTarget(transactions);
    if (!target.has_target) {
        error_out = "nothing to undo";
        return false;
    }
    auto prior = core::audit::ReconstructAtSequence(project, target.target_sequence - 1);
    if (!prior.has_value()) {
        error_out = prior.error();
        return false;
    }
    core::commands::UndoLastTransactionCommand cmd(
        target.target_sequence, target.target_command_name,
        std::move(prior.value().model), std::move(prior.value().package));
    const auto result = bus.Execute(cmd, ctx, "tester");
    if (!result.success) {
        error_out = result.error;
        return false;
    }
    return true;
}

} // namespace

TEST(UndoBoundary, ImplicitInitialSnapshotIsAlwaysTheFloor) {
    // Empty snapshot/baseline lists still pin the wall at sequence 0.
    EXPECT_EQ(core::audit::FindUndoBoundary({}, {}, /*current_sequence=*/5), 0u);
    EXPECT_TRUE(core::audit::CanUndo(/*current_sequence=*/5, /*boundary=*/0));
    EXPECT_FALSE(core::audit::CanUndo(/*current_sequence=*/0, /*boundary=*/0));
}

TEST(UndoBoundary, PicksHighestCheckpointAtOrBeforeCurrent) {
    std::vector<core::audit::SnapshotMetadata> snapshots;
    snapshots.push_back({.snapshot_id = "snapshot_000003", .transaction_sequence = 3});
    snapshots.push_back({.snapshot_id = "snapshot_000007", .transaction_sequence = 7});
    std::vector<core::audit::BaselineMetadata> baselines;
    baselines.push_back({.baseline_id = "b1", .transaction_sequence = 5});

    EXPECT_EQ(core::audit::FindUndoBoundary(snapshots, baselines, /*current=*/10), 7u);
    EXPECT_EQ(core::audit::FindUndoBoundary(snapshots, baselines, /*current=*/6), 5u);
    EXPECT_EQ(core::audit::FindUndoBoundary(snapshots, baselines, /*current=*/3), 3u);
    EXPECT_EQ(core::audit::FindUndoBoundary(snapshots, baselines, /*current=*/2), 0u);
}

TEST(UndoResolver, FindsLatestNonUndoTarget) {
    std::vector<core::audit::AuditTransaction> txs;
    auto make = [](std::uint64_t seq, const std::string& name) {
        core::audit::AuditTransaction tx;
        tx.transaction_sequence = seq;
        tx.command_name         = name;
        core::audit::AuditEvent ev;
        ev.event_sequence       = seq;
        ev.event_type           = name;
        tx.events.push_back(std::move(ev));
        return tx;
    };
    auto make_undo = [](std::uint64_t seq, std::uint64_t undone) {
        core::audit::AuditTransaction tx;
        tx.transaction_sequence = seq;
        tx.command_name         = "Undo";
        core::audit::AuditEvent ev;
        ev.event_sequence       = seq;
        ev.event_type           = "Undo";
        ev.payload              = {{"undone_transaction_sequence", undone},
                                   {"undone_command_name", "Edit"}};
        tx.events.push_back(std::move(ev));
        return tx;
    };

    txs.push_back(make(1, "EditA"));
    txs.push_back(make(2, "EditB"));
    txs.push_back(make_undo(3, /*undone=*/2)); // cancels EditB

    auto target = core::audit::FindUndoTarget(txs);
    ASSERT_TRUE(target.has_target);
    EXPECT_EQ(target.target_sequence, 1u);
    EXPECT_EQ(target.target_command_name, "EditA");

    // Undo-of-undo restores EditB as the most-recent in-force tx.
    txs.push_back(make_undo(4, /*undone=*/3));
    target = core::audit::FindUndoTarget(txs);
    ASSERT_TRUE(target.has_target);
    EXPECT_EQ(target.target_sequence, 2u);
}

TEST(UndoCommand, EmitsUndoEventAndRestoresPriorState) {
    auto f = MakeFixture("emit_event");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add("G1", core::NewElementKind::Strategy);
    const auto add_result = bus->Execute(add, ctx, "tester");
    ASSERT_TRUE(add_result.success) << add_result.error;
    const std::string added_id = add.GeneratedId();
    ASSERT_FALSE(added_id.empty());

    const auto pre_undo_hash = core::audit::CanonicalModelHash(f.package);

    std::string undo_err;
    ASSERT_TRUE(DoUndo(*bus, ctx, f.project, undo_err)) << undo_err;

    // The added strategy is gone from both model and package.
    for (const auto& e : f.model.elements) {
        EXPECT_NE(e.id, added_id);
    }

    // The audit log gained a single Undo transaction referencing tx 1.
    const auto& txs = bus->Store().Transactions();
    ASSERT_EQ(txs.size(), 2u);
    EXPECT_EQ(txs.back().command_name, "Undo");
    ASSERT_EQ(txs.back().events.size(), 1u);
    EXPECT_EQ(txs.back().events.front().event_type, "Undo");
    EXPECT_EQ(txs.back().events.front().payload.at("undone_transaction_sequence").get<std::uint64_t>(), 1u);

    // The post-undo state matches the original (pre-add) snapshot:
    // canonical hash differs from pre_undo_hash and matches what
    // ReconstructAtSequence(0) returns.
    EXPECT_NE(core::audit::CanonicalModelHash(f.package), pre_undo_hash);
    auto reconstructed = core::audit::ReconstructAtSequence(f.project, 0);
    ASSERT_TRUE(reconstructed.has_value()) << reconstructed.error();
    EXPECT_EQ(core::audit::CanonicalModelHash(f.package),
              core::audit::CanonicalModelHash(reconstructed->package));
}

TEST(UndoCommand, ReplayerSkipsUndoneTransactions) {
    auto f = MakeFixture("replayer_skip");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add1("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add1, ctx, "tester").success);
    core::commands::CreateChildElementCommand add2("G1", core::NewElementKind::Context);
    ASSERT_TRUE(bus->Execute(add2, ctx, "tester").success);

    std::string undo_err;
    ASSERT_TRUE(DoUndo(*bus, ctx, f.project, undo_err)) << undo_err; // undoes add2
    const auto live_hash = core::audit::CanonicalModelHash(f.package);

    // Replay from snapshot zero produces the same canonical hash.
    auto replayed = core::audit::ReconstructAtSequence(f.project,
                                                      std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << replayed.error();
    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package), live_hash);
}

TEST(UndoCommand, TwoSuccessiveUndosWalkBackwards) {
    auto f = MakeFixture("two_undos");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add1("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(add1, ctx, "tester").success);
    core::commands::CreateChildElementCommand add2("G1", core::NewElementKind::Context);
    ASSERT_TRUE(bus->Execute(add2, ctx, "tester").success);

    std::string undo_err;
    ASSERT_TRUE(DoUndo(*bus, ctx, f.project, undo_err)) << undo_err; // undoes add2
    ASSERT_TRUE(DoUndo(*bus, ctx, f.project, undo_err)) << undo_err; // undoes add1

    const auto& txs = bus->Store().Transactions();
    ASSERT_EQ(txs.size(), 4u);
    EXPECT_EQ(txs[2].command_name, "Undo");
    EXPECT_EQ(txs[3].command_name, "Undo");
    EXPECT_EQ(txs[2].events.front().payload.at("undone_transaction_sequence").get<std::uint64_t>(), 2u);
    EXPECT_EQ(txs[3].events.front().payload.at("undone_transaction_sequence").get<std::uint64_t>(), 1u);

    // Effective state is the original.
    auto original = core::audit::ReconstructAtSequence(f.project, 0);
    ASSERT_TRUE(original.has_value());
    EXPECT_EQ(core::audit::CanonicalModelHash(f.package),
              core::audit::CanonicalModelHash(original->package));
}

TEST(UndoCommand, NothingToUndoOnFreshProject) {
    auto f = MakeFixture("fresh");
    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    auto target = core::audit::FindUndoTarget(bus->Store().Transactions());
    EXPECT_FALSE(target.has_target);
}

