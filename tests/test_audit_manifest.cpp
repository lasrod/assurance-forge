#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path MakeTempProjectRoot(const std::string& tag) {
    auto root = std::filesystem::temp_directory_path() /
                ("af_test_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

} // namespace

TEST(AuditManifest, RoundTripJson) {
    core::audit::AuditManifest m;
    m.project_id = "proj-123";
    m.created_at = "2025-01-02T03:04:05Z";
    m.created_by = "tester";
    m.current_sacm = "argument.sacm";
    m.initial_snapshot_id = "snapshot_000000";
    m.latest_transaction_sequence = 7;
    m.latest_event_sequence = 42;
    m.last_known_raw_file_hash = "abc";
    m.last_known_canonical_model_hash = "def";
    m.event_store_hash = "012";

    const std::string text = core::audit::SerializeAuditManifest(m);

    core::audit::AuditManifest parsed;
    std::string error;
    ASSERT_TRUE(core::audit::ParseAuditManifest(text, parsed, error)) << error;

    EXPECT_EQ(parsed.manifest_schema_version, core::audit::kManifestSchemaVersion);
    EXPECT_EQ(parsed.project_id, m.project_id);
    EXPECT_EQ(parsed.created_at, m.created_at);
    EXPECT_EQ(parsed.current_sacm, m.current_sacm);
    EXPECT_EQ(parsed.initial_snapshot_id, m.initial_snapshot_id);
    EXPECT_EQ(parsed.latest_transaction_sequence, m.latest_transaction_sequence);
    EXPECT_EQ(parsed.latest_event_sequence, m.latest_event_sequence);
    EXPECT_EQ(parsed.last_known_raw_file_hash, m.last_known_raw_file_hash);
    EXPECT_EQ(parsed.last_known_canonical_model_hash, m.last_known_canonical_model_hash);
    EXPECT_EQ(parsed.event_store_hash, m.event_store_hash);
}

TEST(AuditManifest, WriteAndReadFromDisk) {
    auto root = MakeTempProjectRoot("manifest_io");
    core::audit::AuditManifest m;
    m.project_id = "p";
    m.current_sacm = "x.sacm";
    m.initial_snapshot_id = core::audit::kInitialSnapshotId;

    std::string error;
    ASSERT_TRUE(core::audit::WriteAuditManifest(root, m, error)) << error;
    ASSERT_TRUE(std::filesystem::exists(core::audit::ManifestPath(root)));

    core::audit::AuditManifest loaded;
    ASSERT_TRUE(core::audit::ReadAuditManifest(root, loaded, error)) << error;
    EXPECT_EQ(loaded.project_id, "p");
    EXPECT_EQ(loaded.current_sacm, "x.sacm");
    EXPECT_EQ(loaded.initial_snapshot_id, core::audit::kInitialSnapshotId);

    std::filesystem::remove_all(root);
}

TEST(AuditManifest, RejectsNonObjectJson) {
    core::audit::AuditManifest parsed;
    std::string error;
    EXPECT_FALSE(core::audit::ParseAuditManifest("[1,2,3]", parsed, error));
    EXPECT_FALSE(error.empty());
}
