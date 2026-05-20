#include "core/audit/audit_paths.h"

namespace core::audit {

std::filesystem::path AfDir(const std::filesystem::path& project_root) {
    return project_root / kAfDirName;
}

std::filesystem::path ManifestPath(const std::filesystem::path& project_root) {
    return AfDir(project_root) / kManifestFileName;
}

std::filesystem::path SnapshotsDir(const std::filesystem::path& project_root) {
    return AfDir(project_root) / kSnapshotsDirName;
}

std::filesystem::path SnapshotDir(const std::filesystem::path& project_root, const std::string& snapshot_id) {
    return SnapshotsDir(project_root) / snapshot_id;
}

std::filesystem::path SnapshotSacmPath(const std::filesystem::path& project_root, const std::string& snapshot_id) {
    return SnapshotDir(project_root, snapshot_id) / kSnapshotSacmFileName;
}

std::filesystem::path SnapshotMetadataPath(const std::filesystem::path& project_root, const std::string& snapshot_id) {
    return SnapshotDir(project_root, snapshot_id) / kSnapshotMetadataFileName;
}

std::filesystem::path AuditDir(const std::filesystem::path& project_root) {
    return AfDir(project_root) / kAuditDirName;
}

std::filesystem::path EventLogPath(const std::filesystem::path& project_root) {
    return AuditDir(project_root) / kEventLogFileName;
}

} // namespace core::audit
