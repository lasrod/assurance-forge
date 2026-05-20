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
#include "core/element_factory.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>

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
        snapshot.model, snapshot.package, bus->Store().Transactions(),
        std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());

    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package),
              core::audit::CanonicalModelHash(f.package));

    // The replayed element keeps the id captured in the event payload.
    bool found = false;
    for (const auto& e : replayed->model.elements) {
        if (e.id == cmd.GeneratedId() && e.type == "argumentreasoning") { found = true; break; }
    }
    EXPECT_TRUE(found) << "replayed strategy " << cmd.GeneratedId() << " missing";
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
        snapshot.model, snapshot.package, bus->Store().Transactions(),
        std::numeric_limits<std::uint64_t>::max());
    ASSERT_TRUE(replayed.has_value()) << (replayed.has_value() ? "" : replayed.error());

    EXPECT_EQ(core::audit::CanonicalModelHash(replayed->package),
              core::audit::CanonicalModelHash(f.package));

    // Strategy survived (still in the model); solution was removed.
    bool has_strategy = false, has_solution = false;
    for (const auto& e : replayed->model.elements) {
        if (e.id == strategy_id) has_strategy = true;
        if (e.id == solution_id) has_solution = true;
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
    auto replayed_only_first = core::audit::Replayer::ReplayFrom(
        snapshot.model, snapshot.package, bus->Store().Transactions(), /*up_to=*/1);
    ASSERT_TRUE(replayed_only_first.has_value());

    bool has_a = false, has_b = false;
    for (const auto& e : replayed_only_first->model.elements) {
        if (e.id == a.GeneratedId()) has_a = true;
        if (e.id == b.GeneratedId()) has_b = true;
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
        for (const auto& d : result.diagnostics) s += "\n  - " + d;
        return s;
    }();
    EXPECT_EQ(result.replayed_canonical_hash, result.manifest_canonical_hash);
}

TEST(ReplayVerifier, ReportsMismatchWhenManifestHashIsTamperedWith) {
    auto f = MakeFixture("verifier_manifest_tamper");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;
    core::commands::CommandContext ctx{f.model, f.package};
    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Strategy);
    ASSERT_TRUE(bus->Execute(cmd, ctx, "tester").success);

    // Corrupt the manifest's stored canonical hash.
    core::audit::AuditManifest manifest;
    ASSERT_TRUE(core::audit::ReadAuditManifest(f.project.rootPath, manifest, error));
    manifest.last_known_canonical_model_hash = std::string(64, '0');
    ASSERT_TRUE(core::audit::WriteAuditManifest(f.project.rootPath, manifest, error));

    const auto result = core::audit::VerifyProject(f.project);
    EXPECT_TRUE(result.ran);
    EXPECT_FALSE(result.success);
    bool mentions_manifest = false;
    for (const auto& d : result.diagnostics) {
        if (d.find("manifest") != std::string::npos) mentions_manifest = true;
    }
    EXPECT_TRUE(mentions_manifest);
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
