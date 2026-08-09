#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"
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

constexpr const char* kSampleSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args">
    <claim id="G1" name="Top goal" description="The system is safe."/>
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

std::string ReadAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST(InitialSnapshot, EnsureAuditStoreCreatesManifestSnapshotAndEmptyLog) {
    auto root = MakeTempProjectRoot("init_snap");
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);

    core::AssuranceProject project;
    project.id = "test-project";
    project.name = "Test";
    project.rootPath = root;
    project.lastOpenedWith = "TestRunner";
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult result;
    std::string error;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, result, error)) << error;
    EXPECT_TRUE(result.created);
    EXPECT_EQ(result.snapshot_id, core::audit::kInitialSnapshotId);

    // Files on disk.
    EXPECT_TRUE(std::filesystem::exists(core::audit::ManifestPath(root)));
    EXPECT_TRUE(std::filesystem::exists(core::audit::SnapshotSacmPath(root, core::audit::kInitialSnapshotId)));
    EXPECT_TRUE(std::filesystem::exists(core::audit::SnapshotMetadataPath(root, core::audit::kInitialSnapshotId)));
    EXPECT_TRUE(std::filesystem::exists(core::audit::EventLogPath(root)));
    EXPECT_EQ(std::filesystem::file_size(core::audit::EventLogPath(root)), 0u);

    // Snapshot bytes are byte-identical to source.
    EXPECT_EQ(ReadAllBytes(root / sacm_rel),
              ReadAllBytes(core::audit::SnapshotSacmPath(root, core::audit::kInitialSnapshotId)));

    // Manifest canonical hash matches the hash recomputed from the snapshot
    // through the same library derivation the audit uses (Phase 9 Stage 6).
    core::audit::AuditManifest manifest;
    ASSERT_TRUE(core::audit::ReadAuditManifest(root, manifest, error)) << error;
    auto expected =
        core::library_canonical_hash_from_file(core::audit::SnapshotSacmPath(root, core::audit::kInitialSnapshotId));
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(manifest.last_known_canonical_model_hash, *expected);
    EXPECT_FALSE(manifest.last_known_raw_file_hash.empty());

    std::filesystem::remove_all(root);
}

TEST(InitialSnapshot, EnsureAuditStoreIsIdempotent) {
    auto root = MakeTempProjectRoot("init_snap_idem");
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kSampleSacm);

    core::AssuranceProject project;
    project.id = "p";
    project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult r1;
    std::string error;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, r1, error)) << error;
    EXPECT_TRUE(r1.created);

    const std::string original_manifest = ReadAllBytes(core::audit::ManifestPath(root));

    core::audit::EnsureAuditStoreResult r2;
    ASSERT_TRUE(core::audit::EnsureAuditStore(project, sacm_rel, r2, error)) << error;
    EXPECT_FALSE(r2.created);
    EXPECT_EQ(r2.snapshot_id, r1.snapshot_id);
    EXPECT_EQ(r2.raw_file_hash, r1.raw_file_hash);
    EXPECT_EQ(r2.canonical_model_hash, r1.canonical_model_hash);

    // Manifest file should not have been rewritten.
    EXPECT_EQ(original_manifest, ReadAllBytes(core::audit::ManifestPath(root)));

    std::filesystem::remove_all(root);
}
