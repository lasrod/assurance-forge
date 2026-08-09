#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/audit/canonical_model_hash.h"
#include "core/library_package_projection.h"
#include "core/project_model.h"
#include "legacy_sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

constexpr const char* kSampleSacmV1 = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

constexpr const char* kSampleSacmV2 = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal v2" description="Updated outside the bus."/>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

std::filesystem::path MakeTempProjectRoot(const std::string& tag) {
    auto root = std::filesystem::temp_directory_path() /
                ("af_test_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

core::AssuranceProject MakeProject(const std::filesystem::path& root, const std::filesystem::path& sacm_rel) {
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

TEST(ReconcileAuditStore, RebuildsManifestSnapshotAndEmptyLogFromCurrentSacm) {
    auto root = MakeTempProjectRoot("reconcile");
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacmV1);
    auto project = MakeProject(root, sacm_rel);

    core::audit::EnsureAuditStoreResult init;
    std::string error;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, init, error)) << error;

    // Simulate a transaction having been written to the log; the reconcile
    // must move it out of the way and start a fresh, empty event log.
    {
        std::ofstream log(core::audit::EventLogPath(root), std::ios::binary | std::ios::app);
        log << "{\"transaction_sequence\":1,\"dummy\":true}\n";
    }
    ASSERT_GT(std::filesystem::file_size(core::audit::EventLogPath(root)), 0u);

    // Diverge on-disk SACM from the recorded snapshot.
    WriteFile(root / sacm_rel, kSampleSacmV2);

    core::audit::ReconcileAuditStoreResult reconcile;
    ASSERT_TRUE(core::audit::ReconcileAuditStore(project, sacm_rel, reconcile, error)) << error;
    EXPECT_FALSE(reconcile.backup_dir.empty());
    EXPECT_EQ(reconcile.snapshot_id, core::audit::kInitialSnapshotId);

    // Backup directory must contain the prior artifacts.
    const std::filesystem::path backup = reconcile.backup_dir;
    EXPECT_TRUE(std::filesystem::exists(backup / core::audit::kManifestFileName));
    EXPECT_TRUE(std::filesystem::exists(backup / core::audit::kSnapshotsDirName));
    EXPECT_TRUE(std::filesystem::exists(backup / core::audit::kAuditDirName));

    // Fresh store reflects the current SACM file.
    EXPECT_TRUE(std::filesystem::exists(core::audit::ManifestPath(root)));
    EXPECT_EQ(std::filesystem::file_size(core::audit::EventLogPath(root)), 0u);

    auto live_hash = core::library_canonical_hash_from_file(root / sacm_rel);
    ASSERT_TRUE(live_hash.has_value());

    core::audit::AuditManifest manifest;
    ASSERT_TRUE(core::audit::ReadAuditManifest(root, manifest, error)) << error;
    EXPECT_EQ(manifest.last_known_canonical_model_hash, *live_hash);
    EXPECT_EQ(manifest.latest_transaction_sequence, 0u);
    EXPECT_EQ(manifest.latest_event_sequence, 0u);

    std::filesystem::remove_all(root);
}

TEST(ReconcileAuditStore, BuildsUniqueBackupDirOnRepeatedCalls) {
    auto root = MakeTempProjectRoot("reconcile_repeat");
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacmV1);
    auto project = MakeProject(root, sacm_rel);

    core::audit::EnsureAuditStoreResult init;
    std::string error;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, init, error)) << error;

    core::audit::ReconcileAuditStoreResult r1;
    ASSERT_TRUE(core::audit::ReconcileAuditStore(project, sacm_rel, r1, error)) << error;
    core::audit::ReconcileAuditStoreResult r2;
    ASSERT_TRUE(core::audit::ReconcileAuditStore(project, sacm_rel, r2, error)) << error;
    EXPECT_NE(r1.backup_dir, r2.backup_dir);
    EXPECT_TRUE(std::filesystem::exists(r1.backup_dir));
    EXPECT_TRUE(std::filesystem::exists(r2.backup_dir));

    std::filesystem::remove_all(root);
}
