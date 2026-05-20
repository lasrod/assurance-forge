#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
#include "core/audit/audit_store.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_store.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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
                ("af_cmdbus_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
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
    std::filesystem::path  sacm_abs;
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

} // namespace

TEST(CommandBus, AppendsTransactionAndUpdatesManifestOnCreateChildElement) {
    auto f = MakeFixture("create_child");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Strategy);
    core::commands::CommandContext ctx{f.model, f.package};
    const auto result = bus->Execute(cmd, ctx, "tester");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.transaction_sequence, 1u);
    EXPECT_FALSE(result.transaction_id.empty());
    EXPECT_FALSE(result.raw_file_hash_after.empty());
    EXPECT_FALSE(result.canonical_model_hash_after.empty());
    EXPECT_FALSE(cmd.GeneratedId().empty());

    // Manifest reflects new sequence and the post-state hashes.
    core::audit::AuditManifest manifest;
    ASSERT_TRUE(core::audit::ReadAuditManifest(f.project.rootPath, manifest, error)) << error;
    EXPECT_EQ(manifest.latest_transaction_sequence, 1u);
    EXPECT_EQ(manifest.latest_event_sequence, 1u);
    EXPECT_EQ(manifest.last_known_raw_file_hash, result.raw_file_hash_after);
    EXPECT_EQ(manifest.last_known_canonical_model_hash, result.canonical_model_hash_after);
    EXPECT_FALSE(manifest.event_store_hash.empty());

    // The on-disk SACM contains the new element (via re-parse): the strategy
    // we just added should appear in the first argument package.
    auto reparsed = sacm::parse_sacm(f.sacm_abs.string());
    ASSERT_TRUE(reparsed.has_value()) << reparsed.error();
    ASSERT_FALSE(reparsed.value().argumentPackages.empty());
    const auto& ap = reparsed.value().argumentPackages.front();
    bool found = false;
    for (const auto& reasoning : ap.argumentReasonings) {
        if (reasoning.id == cmd.GeneratedId()) { found = true; break; }
    }
    EXPECT_TRUE(found) << "newly-added strategy " << cmd.GeneratedId() << " not present on disk";
}

TEST(CommandBus, FailedCommandLeavesNoTransactionOrManifestChange) {
    auto f = MakeFixture("fail_cmd");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    const auto manifest_before = bus->Manifest();
    const std::uint64_t txn_before = bus->Store().LatestTransactionSequence();

    core::commands::CreateChildElementCommand cmd("does-not-exist", core::NewElementKind::Goal);
    core::commands::CommandContext ctx{f.model, f.package};
    const auto result = bus->Execute(cmd, ctx, "tester");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
    EXPECT_EQ(bus->Store().LatestTransactionSequence(), txn_before);
    EXPECT_EQ(bus->Manifest().latest_transaction_sequence, manifest_before.latest_transaction_sequence);
    EXPECT_EQ(bus->Manifest().event_store_hash, manifest_before.event_store_hash);
}

TEST(CommandBus, ConsecutiveCommandsAdvanceSequences) {
    auto f = MakeFixture("multi_cmd");

    std::string error;
    auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus) << error;

    core::commands::CommandContext ctx{f.model, f.package};

    core::commands::CreateChildElementCommand add1("G1", core::NewElementKind::Strategy);
    auto r1 = bus->Execute(add1, ctx, "tester");
    ASSERT_TRUE(r1.success) << r1.error;
    EXPECT_EQ(r1.transaction_sequence, 1u);

    core::commands::CreateChildElementCommand add2("G1", core::NewElementKind::Context);
    auto r2 = bus->Execute(add2, ctx, "tester");
    ASSERT_TRUE(r2.success) << r2.error;
    EXPECT_EQ(r2.transaction_sequence, 2u);
    EXPECT_NE(r1.raw_file_hash_after, r2.raw_file_hash_after);

    core::commands::RemoveElementCommand rm(add1.GeneratedId(), core::RemoveMode::NodeOnly);
    auto r3 = bus->Execute(rm, ctx, "tester");
    ASSERT_TRUE(r3.success) << r3.error;
    EXPECT_EQ(r3.transaction_sequence, 3u);
}

TEST(CommandBus, ReopenSeesPriorTransactions) {
    auto f = MakeFixture("reopen");

    std::string error;
    {
        auto bus = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
        ASSERT_TRUE(bus) << error;
        core::commands::CreateChildElementCommand cmd("G1", core::NewElementKind::Solution);
        core::commands::CommandContext ctx{f.model, f.package};
        ASSERT_TRUE(bus->Execute(cmd, ctx, "tester").success);
    }

    auto bus2 = core::commands::CommandBus::Open(f.project, f.sacm_abs, error);
    ASSERT_TRUE(bus2) << error;
    EXPECT_EQ(bus2->Store().LatestTransactionSequence(), 1u);
    EXPECT_EQ(bus2->Manifest().latest_transaction_sequence, 1u);
}

TEST(ElementCommands, KindAndModeTokensRoundTrip) {
    using core::NewElementKind;
    using core::RemoveMode;
    NewElementKind k;
    ASSERT_TRUE(core::commands::NewElementKindFromToken("Strategy", k));
    EXPECT_EQ(k, NewElementKind::Strategy);
    ASSERT_TRUE(core::commands::NewElementKindFromToken("Justification", k));
    EXPECT_EQ(k, NewElementKind::Justification);
    EXPECT_FALSE(core::commands::NewElementKindFromToken("Nope", k));
    EXPECT_EQ(core::commands::NewElementKindToToken(NewElementKind::Context), "Context");

    RemoveMode m;
    ASSERT_TRUE(core::commands::RemoveModeFromToken("NodeAndDescendants", m));
    EXPECT_EQ(m, RemoveMode::NodeAndDescendants);
    EXPECT_EQ(core::commands::RemoveModeToToken(RemoveMode::NodeOnly), "NodeOnly");
}
