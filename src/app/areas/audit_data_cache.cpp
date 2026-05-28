#include "app/areas/audit_data_cache.h"

#include "core/audit/audit_paths.h"
#include "core/audit/event_store.h"

#include <chrono>
#include <system_error>
#include <unordered_map>

namespace app::areas {

namespace {

using FileTime = std::filesystem::file_time_type;

struct FileStamp {
    bool exists = false;
    FileTime mtime{};
    std::uintmax_t size = 0;

    bool operator==(const FileStamp& other) const {
        return exists == other.exists && mtime == other.mtime && size == other.size;
    }
    bool operator!=(const FileStamp& other) const { return !(*this == other); }
};

FileStamp StatFile(const std::filesystem::path& path) {
    FileStamp out;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return out;
    }
    out.exists = true;
    out.mtime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        out.exists = false;
        return out;
    }
    // For directories `file_size` is unspecified; only use it for regular
    // files. The mtime alone is the change signal for directories.
    if (std::filesystem::is_regular_file(path, ec)) {
        out.size = std::filesystem::file_size(path, ec);
        if (ec)
            out.size = 0;
    }
    return out;
}

struct TransactionCache {
    FileStamp log_stamp;
    std::vector<core::audit::AuditTransaction> transactions;
    std::string last_error;
    bool loaded = false;
};

struct BaselineCache {
    FileStamp manifest_stamp;
    FileStamp dir_stamp;
    std::vector<core::audit::BaselineMetadata> baselines;
    std::vector<std::string> warnings;
    bool loaded = false;
};

struct SnapshotCache {
    FileStamp dir_stamp;
    std::vector<core::audit::SnapshotMetadata> snapshots;
    bool loaded = false;
};

std::unordered_map<std::string, TransactionCache>& TxCacheMap() {
    static std::unordered_map<std::string, TransactionCache> m;
    return m;
}

std::unordered_map<std::string, BaselineCache>& BaselineCacheMap() {
    static std::unordered_map<std::string, BaselineCache> m;
    return m;
}

std::unordered_map<std::string, SnapshotCache>& SnapshotCacheMap() {
    static std::unordered_map<std::string, SnapshotCache> m;
    return m;
}

} // namespace

const std::vector<core::audit::AuditTransaction>&
GetCachedTransactions(const std::filesystem::path& project_root, std::string& error_out) {
    const std::string key = project_root.string();
    TransactionCache& cache = TxCacheMap()[key];

    const auto log_path = core::audit::EventLogPath(project_root);
    const FileStamp stamp = StatFile(log_path);

    if (cache.loaded && stamp == cache.log_stamp) {
        error_out = cache.last_error;
        return cache.transactions;
    }

    std::string err;
    auto store = core::audit::EventStore::Open(project_root, err);
    if (!store) {
        // Keep prior data so the UI does not flicker on transient errors.
        cache.last_error = std::move(err);
        error_out = cache.last_error;
        cache.log_stamp = stamp;
        cache.loaded = true;
        return cache.transactions;
    }
    cache.transactions = store->Transactions();
    cache.last_error.clear();
    cache.log_stamp = stamp;
    cache.loaded = true;
    error_out.clear();
    return cache.transactions;
}

const std::vector<core::audit::BaselineMetadata>&
GetCachedBaselines(const std::filesystem::path& project_root, std::vector<std::string>* warnings_out) {
    const std::string key = project_root.string();
    BaselineCache& cache = BaselineCacheMap()[key];

    const auto manifest_path = core::audit::ManifestPath(project_root);
    const auto baselines_dir = core::audit::BaselinesDir(project_root);
    const FileStamp manifest_stamp = StatFile(manifest_path);
    const FileStamp dir_stamp = StatFile(baselines_dir);

    if (cache.loaded && manifest_stamp == cache.manifest_stamp && dir_stamp == cache.dir_stamp) {
        if (warnings_out)
            *warnings_out = cache.warnings;
        return cache.baselines;
    }

    std::vector<std::string> warnings;
    cache.baselines = core::audit::ListBaselines(project_root, &warnings);
    cache.warnings = std::move(warnings);
    cache.manifest_stamp = manifest_stamp;
    cache.dir_stamp = dir_stamp;
    cache.loaded = true;
    if (warnings_out)
        *warnings_out = cache.warnings;
    return cache.baselines;
}

const std::vector<core::audit::SnapshotMetadata>&
GetCachedSnapshots(const std::filesystem::path& project_root) {
    const std::string key = project_root.string();
    SnapshotCache& cache = SnapshotCacheMap()[key];

    const auto snap_dir = core::audit::SnapshotsDir(project_root);
    const FileStamp dir_stamp = StatFile(snap_dir);

    if (cache.loaded && dir_stamp == cache.dir_stamp) {
        return cache.snapshots;
    }

    std::vector<core::audit::SnapshotMetadata> snapshots;
    std::error_code ec;
    if (std::filesystem::exists(snap_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(snap_dir, ec)) {
            if (!entry.is_directory())
                continue;
            const std::string snap_id = entry.path().filename().string();
            core::audit::SnapshotMetadata md;
            std::string err;
            if (core::audit::ReadSnapshotMetadata(project_root, snap_id, md, err))
                snapshots.push_back(std::move(md));
        }
    }

    cache.snapshots = std::move(snapshots);
    cache.dir_stamp = dir_stamp;
    cache.loaded = true;
    return cache.snapshots;
}

void ClearAuditDataCache() {
    TxCacheMap().clear();
    BaselineCacheMap().clear();
    SnapshotCacheMap().clear();
}

} // namespace app::areas
