#pragma once

#include <filesystem>
#include <string>

// Single source of truth for on-disk paths used by the audit / event store
// subsystem (design §5: `.af/` layout). All helpers take the project root
// (absolute) and return absolute paths.
namespace core::audit {

inline constexpr const char* kAfDirName = ".af";
inline constexpr const char* kManifestFileName = "manifest.af.json";
inline constexpr const char* kSnapshotsDirName = "snapshots";
inline constexpr const char* kAuditDirName = "audit";
inline constexpr const char* kEventLogFileName = "transactions.af.jsonl";
inline constexpr const char* kInitialSnapshotId = "snapshot_000000";
inline constexpr const char* kSnapshotSacmFileName = "main.sacm";
inline constexpr const char* kSnapshotMetadataFileName = "snapshot.af.json";
inline constexpr const char* kBaselinesDirName = "baselines";
inline constexpr const char* kBaselineFileNameSuffix = ".json";

std::filesystem::path AfDir(const std::filesystem::path& project_root);
std::filesystem::path ManifestPath(const std::filesystem::path& project_root);
std::filesystem::path SnapshotsDir(const std::filesystem::path& project_root);
std::filesystem::path SnapshotDir(const std::filesystem::path& project_root, const std::string& snapshot_id);
std::filesystem::path SnapshotSacmPath(const std::filesystem::path& project_root, const std::string& snapshot_id);
std::filesystem::path SnapshotMetadataPath(const std::filesystem::path& project_root, const std::string& snapshot_id);
std::filesystem::path AuditDir(const std::filesystem::path& project_root);
std::filesystem::path EventLogPath(const std::filesystem::path& project_root);
std::filesystem::path BaselinesDir(const std::filesystem::path& project_root);
std::filesystem::path BaselineMetadataPath(const std::filesystem::path& project_root,
                                           const std::string& baseline_id);

} // namespace core::audit
