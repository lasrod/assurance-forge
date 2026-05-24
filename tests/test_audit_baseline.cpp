// Tests for the baseline data layer (core::audit::BaselineMetadata).
//
// Baselines are lightweight pointers into the transaction log — no SACM
// byte copy — and the on-disk representation must be forward-compatible:
// unknown fields are ignored, missing fields default safely, and the
// manifest's `baseline_ids` index is updated atomically with the sidecar
// write.

#include "core/audit/audit_baseline.h"
#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

using core::audit::BaselineMetadata;
using core::audit::ParseBaselineMetadata;
using core::audit::SerializeBaselineMetadata;

std::filesystem::path MakeTempProjectRoot(const std::string& suffix) {
    auto root = std::filesystem::temp_directory_path() /
                ("af_baseline_test_" + suffix);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(core::audit::AfDir(root));
    return root;
}

void WriteBootstrapManifest(const std::filesystem::path& project_root) {
    core::audit::AuditManifest m;
    m.project_id = "test-project";
    m.created_at = "2025-01-01T00:00:00Z";
    m.created_by = "tester";
    m.current_sacm = "case.sacm.xml";
    m.initial_snapshot_id = "snapshot_000000";
    m.latest_transaction_sequence = 7;
    std::string err;
    ASSERT_TRUE(core::audit::WriteAuditManifest(project_root, m, err)) << err;
}

TEST(BaselineMetadata, SerializeRoundtrip) {
    BaselineMetadata md;
    md.baseline_id = "baseline_000001";
    md.name = "First baseline";
    md.description = "Locked at hazard analysis review";
    md.created_at = "2025-04-05T10:00:00Z";
    md.created_by = "alice";
    md.transaction_sequence = 42;
    md.canonical_model_hash = "abc123";

    const std::string text = SerializeBaselineMetadata(md);
    BaselineMetadata parsed;
    std::string err;
    ASSERT_TRUE(ParseBaselineMetadata(text, parsed, err)) << err;
    EXPECT_EQ(parsed.baseline_id, md.baseline_id);
    EXPECT_EQ(parsed.name, md.name);
    EXPECT_EQ(parsed.description, md.description);
    EXPECT_EQ(parsed.created_at, md.created_at);
    EXPECT_EQ(parsed.created_by, md.created_by);
    EXPECT_EQ(parsed.transaction_sequence, md.transaction_sequence);
    EXPECT_EQ(parsed.canonical_model_hash, md.canonical_model_hash);
}

TEST(BaselineMetadata, ParseTolerantOfUnknownFields) {
    const std::string text = R"({
        "baseline_schema_version": 1,
        "baseline_id": "baseline_000002",
        "name": "Reviewer-approved",
        "transaction_sequence": 11,
        "canonical_model_hash": "deadbeef",
        "future_field_we_dont_know": {"x": 1}
    })";
    BaselineMetadata md;
    std::string err;
    ASSERT_TRUE(ParseBaselineMetadata(text, md, err)) << err;
    EXPECT_EQ(md.baseline_id, "baseline_000002");
    EXPECT_EQ(md.transaction_sequence, 11u);
}

TEST(BaselineMetadata, CreateBaselineAppendsIndexAndWritesSidecar) {
    const auto root = MakeTempProjectRoot("create");
    WriteBootstrapManifest(root);

    core::audit::CreateBaselineRequest req;
    req.name = "Hazard analysis baseline";
    req.description = "Locked after L1 review";
    req.created_by = "alice";
    req.canonical_model_hash = "hash-at-seq-7";
    // at_transaction_sequence not set → should default to manifest latest (7).

    BaselineMetadata created;
    std::string err;
    ASSERT_TRUE(core::audit::CreateBaseline(root, req, created, err)) << err;
    EXPECT_EQ(created.transaction_sequence, 7u);
    EXPECT_FALSE(created.baseline_id.empty());
    EXPECT_FALSE(created.created_at.empty());

    // Sidecar file exists.
    EXPECT_TRUE(std::filesystem::exists(
        core::audit::BaselineMetadataPath(root, created.baseline_id)));

    // Manifest now contains the id.
    core::audit::AuditManifest reloaded;
    ASSERT_TRUE(core::audit::ReadAuditManifest(root, reloaded, err)) << err;
    ASSERT_EQ(reloaded.baseline_ids.size(), 1u);
    EXPECT_EQ(reloaded.baseline_ids[0], created.baseline_id);

    // ListBaselines returns it.
    auto listed = core::audit::ListBaselines(root);
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].baseline_id, created.baseline_id);
    EXPECT_EQ(listed[0].name, req.name);
}

TEST(BaselineMetadata, ListBaselinesSortedBySequence) {
    const auto root = MakeTempProjectRoot("listsort");
    WriteBootstrapManifest(root);

    auto make = [&](const std::string& name, std::uint64_t at_seq) {
        core::audit::CreateBaselineRequest req;
        req.name = name;
        req.created_by = "tester";
        req.canonical_model_hash = "h";
        req.at_transaction_sequence = at_seq;
        BaselineMetadata md;
        std::string err;
        ASSERT_TRUE(core::audit::CreateBaseline(root, req, md, err)) << err;
    };
    make("third", 7);
    make("first", 1);
    make("second", 4);

    auto listed = core::audit::ListBaselines(root);
    ASSERT_EQ(listed.size(), 3u);
    EXPECT_EQ(listed[0].name, "first");
    EXPECT_EQ(listed[1].name, "second");
    EXPECT_EQ(listed[2].name, "third");
}

} // namespace
