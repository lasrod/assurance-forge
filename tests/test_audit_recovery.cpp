#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_recovery.h"
#include "core/audit/audit_store.h"
#include "core/audit/canonical_model_hash.h"
#include "core/audit/event_store.h"
#include "core/audit/replay_verifier.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
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

constexpr const char* kTamperedSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="Edited outside the bus."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

std::filesystem::path MakeTempProjectRoot(const std::string& tag) {
    auto root = std::filesystem::temp_directory_path() /
                ("af_test_" + tag + "_" +
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

core::AssuranceProject MakeProject(const std::filesystem::path& root,
                                   const std::filesystem::path& sacm_rel) {
    core::AssuranceProject project;
    project.id = "p";
    project.rootPath = root;
    project.lastOpenedWith = "TestRunner";
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    project.files.push_back(entry);
    return project;
}

} // namespace

TEST(AuditRecovery, RestoreSacmFromAuditRewritesDivergedFile) {
    auto root = MakeTempProjectRoot("restore_basic");
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);
    auto project = MakeProject(root, sacm_rel);

    core::audit::EnsureAuditStoreResult init;
    std::string err;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, init, err)) << err;

    // Tamper with on-disk SACM (simulates a write that bypassed CommandBus).
    WriteFile(root / sacm_rel, kTamperedSacm);

    // Sanity: verifier reports replay-vs-disk divergence.
    auto v_before = core::audit::VerifyProject(project);
    ASSERT_TRUE(v_before.ran);
    ASSERT_FALSE(v_before.success);
    EXPECT_EQ(v_before.cause, core::audit::DivergenceCause::ReplayDoesNotMatchOnDisk);

    core::audit::RestoreSacmFromAuditResult result;
    ASSERT_TRUE(core::audit::RestoreSacmFromAudit(project, sacm_rel, "alice", result, err)) << err;

    // After restore the file should round-trip to the snapshot canonical hash.
    auto restored_pkg = sacm::parse_sacm((root / sacm_rel).string());
    ASSERT_TRUE(restored_pkg) << restored_pkg.error();
    EXPECT_EQ(core::audit::CanonicalModelHash(*restored_pkg), result.restored_canonical_hash);
    EXPECT_FALSE(result.pre_restore_on_disk_canonical_hash.empty());
    EXPECT_NE(result.pre_restore_on_disk_canonical_hash, result.restored_canonical_hash);
    EXPECT_GT(result.transaction_sequence, 0u);

    // Verifier should now report success.
    auto v_after = core::audit::VerifyProject(project);
    EXPECT_TRUE(v_after.success) << (v_after.diagnostics.empty() ? "" : v_after.diagnostics.front());
    EXPECT_EQ(v_after.cause, core::audit::DivergenceCause::None);
}

TEST(AuditRecovery, RestoreSacmFromAuditAppendsAuditTransaction) {
    auto root = MakeTempProjectRoot("restore_appends");
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);
    auto project = MakeProject(root, sacm_rel);

    core::audit::EnsureAuditStoreResult init;
    std::string err;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, init, err)) << err;

    WriteFile(root / sacm_rel, kTamperedSacm);

    core::audit::RestoreSacmFromAuditResult result;
    ASSERT_TRUE(core::audit::RestoreSacmFromAudit(project, sacm_rel, "alice", result, err)) << err;

    auto store = core::audit::EventStore::Open(root, err);
    ASSERT_TRUE(store) << err;
    ASSERT_EQ(store->Transactions().size(), 1u);
    const auto& tx = store->Transactions().front();
    EXPECT_EQ(tx.command_name, "RestoreSacmFromAudit");
    EXPECT_EQ(tx.author, "alice");
    ASSERT_EQ(tx.events.size(), 1u);
    EXPECT_EQ(tx.events.front().event_type, "SacmRestoredFromAudit");
    const auto& payload = tx.events.front().payload;
    EXPECT_EQ(payload.at("sacm_path").get<std::string>(), "argument.sacm");
    EXPECT_EQ(payload.at("restored_canonical_hash").get<std::string>(), result.restored_canonical_hash);
    EXPECT_FALSE(payload.at("pre_restore_canonical_hash").get<std::string>().empty());
}

TEST(AuditRecovery, RestoreSacmFromAuditPreservesPriorTransactionsOnReplay) {
    auto root = MakeTempProjectRoot("restore_preserves");
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);
    auto project = MakeProject(root, sacm_rel);

    core::audit::EnsureAuditStoreResult init;
    std::string err;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, init, err)) << err;

    // Issue one legitimate edit through the bus.
    {
        auto bus = core::commands::CommandBus::Open(project, root / sacm_rel, err);
        ASSERT_TRUE(bus) << err;
        auto pkg = sacm::parse_sacm((root / sacm_rel).string());
        ASSERT_TRUE(pkg);
        auto model = parser::parse_sacm_xml((root / sacm_rel).string());
        ASSERT_TRUE(model);
        core::commands::CommandContext ctx{*model, *pkg};

        core::commands::UpdateElementTextCommand cmd{
            "G1", core::ElementTextField::Description, "en", "Audited edit."};
        auto res = bus->Execute(cmd, ctx, "alice");
        ASSERT_TRUE(res.success) << res.error;
    }

    // Now corrupt the on-disk SACM by hand.
    WriteFile(root / sacm_rel, kTamperedSacm);

    core::audit::RestoreSacmFromAuditResult result;
    ASSERT_TRUE(core::audit::RestoreSacmFromAudit(project, sacm_rel, "alice", result, err)) << err;

    // The restored on-disk SACM must reflect the audited edit, not the
    // tampered text and not the original snapshot.
    auto restored = sacm::parse_sacm((root / sacm_rel).string());
    ASSERT_TRUE(restored) << restored.error();
    // Walk into the package to find G1 description.
    bool found = false;
    for (const auto& ap : restored->argumentPackages) {
        for (const auto& claim : ap.claims) {
            if (claim.id == "G1") {
                EXPECT_EQ(claim.description, "Audited edit.");
                found = true;
            }
        }
    }
    EXPECT_TRUE(found);

    // Replay continues to work: there should be 2 transactions in the log.
    auto store = core::audit::EventStore::Open(root, err);
    ASSERT_TRUE(store) << err;
    EXPECT_EQ(store->Transactions().size(), 2u);
}

TEST(AuditRecovery, RestoreRejectsMissingProjectRoot) {
    core::AssuranceProject project;
    core::audit::RestoreSacmFromAuditResult result;
    std::string err;
    EXPECT_FALSE(core::audit::RestoreSacmFromAudit(project, "argument.sacm", "alice", result, err));
    EXPECT_FALSE(err.empty());
}
