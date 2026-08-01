// Tests for core::audit::CreateUserSnapshot.
//
// A user snapshot writes a verbatim copy of the project's current SACM
// file into `<project_root>/.af/snapshots/<snapshot_id>/`, derives a
// stable id from the manifest's latest_transaction_sequence, and stamps
// the metadata sidecar with the reason and creator supplied by the
// caller. The function must refuse to overwrite an existing snapshot
// at the same sequence so user actions cannot silently clobber prior
// state.

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/audit/audit_snapshot.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path MakeTempProjectRoot(const std::string& suffix) {
    auto root = std::filesystem::temp_directory_path() / ("af_user_snapshot_test_" + suffix);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(core::audit::AfDir(root));
    return root;
}

void WriteSacm(const std::filesystem::path& path, const std::string& body) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << body;
}

// Returns true on success; failure detail goes into `error`. ASSERT_* is
// avoided here because gtest's fatal-assert macros inside a void helper
// only return from the helper, leaving the caller to continue with
// half-initialized state. The caller is expected to assert on the result.
[[nodiscard]] bool
WriteManifest(const std::filesystem::path& project_root, std::uint64_t latest_seq, std::string& error) {
    core::audit::AuditManifest m;
    m.project_id = "test-project";
    m.created_at = "2025-01-01T00:00:00Z";
    m.created_by = "tester";
    m.current_sacm = "case.sacm.xml";
    m.initial_snapshot_id = "snapshot_000000";
    m.latest_transaction_sequence = latest_seq;
    m.latest_event_sequence = latest_seq;
    return core::audit::WriteAuditManifest(project_root, m, error);
}

constexpr const char* kMinimalSacm = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                     "<sacm:AssuranceCasePackage xmlns:sacm=\"http://www.omg.org/sacm/2.3\" "
                                     "xmi:id=\"acp1\" xmlns:xmi=\"http://www.omg.org/spec/XMI/20131001\"/>";

TEST(CreateUserSnapshot, WritesSidecarAndCopiesSacm) {
    auto root = MakeTempProjectRoot("writes");
    std::string manifest_err;
    ASSERT_TRUE(WriteManifest(root, 42, manifest_err)) << manifest_err;
    WriteSacm(root / "case.sacm.xml", kMinimalSacm);

    core::audit::SnapshotMetadata md;
    std::string err;
    ASSERT_TRUE(core::audit::CreateUserSnapshot(root, "manual save", "alice", md, err)) << err;

    EXPECT_EQ(md.snapshot_id, "snapshot_000042");
    EXPECT_EQ(md.transaction_sequence, 42u);
    EXPECT_EQ(md.created_by, "alice");
    EXPECT_EQ(md.reason, "manual save");
    EXPECT_FALSE(md.raw_file_hash.empty());

    // SACM bytes copied verbatim.
    auto copied = core::audit::SnapshotSacmPath(root, md.snapshot_id);
    ASSERT_TRUE(std::filesystem::exists(copied));
    std::ifstream in(copied, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, std::string(kMinimalSacm));

    // Sidecar readable round-trip.
    core::audit::SnapshotMetadata reread;
    std::string read_err;
    ASSERT_TRUE(core::audit::ReadSnapshotMetadata(root, md.snapshot_id, reread, read_err)) << read_err;
    EXPECT_EQ(reread.transaction_sequence, 42u);
    EXPECT_EQ(reread.reason, "manual save");
}

TEST(CreateUserSnapshot, RefusesDuplicateAtSameSequence) {
    auto root = MakeTempProjectRoot("dup");
    std::string manifest_err;
    ASSERT_TRUE(WriteManifest(root, 5, manifest_err)) << manifest_err;
    WriteSacm(root / "case.sacm.xml", kMinimalSacm);

    core::audit::SnapshotMetadata md1;
    std::string err1;
    ASSERT_TRUE(core::audit::CreateUserSnapshot(root, "first", "alice", md1, err1)) << err1;

    core::audit::SnapshotMetadata md2;
    std::string err2;
    EXPECT_FALSE(core::audit::CreateUserSnapshot(root, "second", "alice", md2, err2));
    EXPECT_FALSE(err2.empty());
}

TEST(CreateUserSnapshot, FailsWhenManifestMissing) {
    auto root = MakeTempProjectRoot("nomanifest");
    // No manifest written.

    core::audit::SnapshotMetadata md;
    std::string err;
    EXPECT_FALSE(core::audit::CreateUserSnapshot(root, "x", "alice", md, err));
    EXPECT_FALSE(err.empty());
}

} // namespace
