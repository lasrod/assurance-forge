#include "core/audit/audit_snapshot.h"

#include "core/audit/audit_paths.h"
#include "core/audit/canonical_model_hash.h"
#include "core/project_file_io.h"
#include "core/sha256.h"
#include "core/time_utils.h"
#include "sacm/sacm_parser.h"

#include <nlohmann/json.hpp>

#include <exception>

namespace core::audit {

namespace {

using nlohmann::json;

template <typename T>
T ReadOr(const json& j, const char* key, T fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
        return fallback;
    try {
        return it->get<T>();
    } catch (const std::exception&) {
        return fallback;
    }
}

} // namespace

std::string SerializeSnapshotMetadata(const SnapshotMetadata& m) {
    json j;
    j["snapshot_schema_version"] = m.snapshot_schema_version;
    j["snapshot_id"] = m.snapshot_id;
    j["created_at"] = m.created_at;
    j["created_by"] = m.created_by;
    j["reason"] = m.reason;
    j["transaction_sequence"] = m.transaction_sequence;
    j["event_sequence"] = m.event_sequence;
    j["raw_file_hash"] = m.raw_file_hash;
    j["canonical_model_hash"] = m.canonical_model_hash;
    return j.dump(2) + "\n";
}

bool ParseSnapshotMetadata(const std::string& text, SnapshotMetadata& out, std::string& error) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& ex) {
        error = std::string("Invalid snapshot metadata JSON: ") + ex.what();
        return false;
    }
    if (!j.is_object()) {
        error = "Snapshot metadata root is not a JSON object";
        return false;
    }
    out = SnapshotMetadata{};
    out.snapshot_schema_version = ReadOr<int>(j, "snapshot_schema_version", kSnapshotSchemaVersion);
    out.snapshot_id = ReadOr<std::string>(j, "snapshot_id", {});
    out.created_at = ReadOr<std::string>(j, "created_at", {});
    out.created_by = ReadOr<std::string>(j, "created_by", {});
    out.reason = ReadOr<std::string>(j, "reason", {});
    out.transaction_sequence = ReadOr<std::uint64_t>(j, "transaction_sequence", 0);
    out.event_sequence = ReadOr<std::uint64_t>(j, "event_sequence", 0);
    out.raw_file_hash = ReadOr<std::string>(j, "raw_file_hash", {});
    out.canonical_model_hash = ReadOr<std::string>(j, "canonical_model_hash", {});
    return true;
}

bool ReadSnapshotMetadata(const std::filesystem::path& project_root,
                          const std::string& snapshot_id,
                          SnapshotMetadata& out,
                          std::string& error) {
    auto text = ReadTextFile(SnapshotMetadataPath(project_root, snapshot_id));
    if (!text) {
        error = std::move(text.error());
        return false;
    }
    return ParseSnapshotMetadata(*text, out, error);
}

bool WriteSnapshotMetadata(const std::filesystem::path& project_root,
                           const SnapshotMetadata& metadata,
                           std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(SnapshotDir(project_root, metadata.snapshot_id), ec);
    if (ec) {
        error = "Could not create snapshot directory: " + ec.message();
        return false;
    }
    auto r = WriteTextFile(SnapshotMetadataPath(project_root, metadata.snapshot_id),
                           SerializeSnapshotMetadata(metadata));
    if (!r) {
        error = std::move(r.error());
        return false;
    }
    return true;
}

bool CreateInitialSnapshot(const std::filesystem::path& project_root,
                           const std::filesystem::path& source_sacm_absolute_path,
                           const std::string& created_by,
                           SnapshotMetadata& out_metadata,
                           std::string& error) {
    const std::string snapshot_id = kInitialSnapshotId;
    const std::filesystem::path snapshot_dir = SnapshotDir(project_root, snapshot_id);
    const std::filesystem::path snapshot_sacm = SnapshotSacmPath(project_root, snapshot_id);
    const std::filesystem::path snapshot_meta = SnapshotMetadataPath(project_root, snapshot_id);

    std::error_code ec;
    if (std::filesystem::exists(snapshot_meta, ec)) {
        if (!ReadSnapshotMetadata(project_root, snapshot_id, out_metadata, error))
            return false;
        return true;
    }

    if (!std::filesystem::create_directories(snapshot_dir, ec) && ec) {
        error = "Could not create snapshot directory: " + ec.message();
        return false;
    }

    auto bytes = ReadFileBytes(source_sacm_absolute_path);
    if (!bytes) {
        error = std::move(bytes.error());
        return false;
    }

    auto write = WriteTextFile(snapshot_sacm,
                               std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    if (!write) {
        error = std::move(write.error());
        return false;
    }

    const std::string raw_hash = Sha256::HexDigest(*bytes);

    std::string canonical_hash;
    auto parsed = sacm::parse_sacm(source_sacm_absolute_path.string());
    if (parsed) {
        canonical_hash = CanonicalModelHash(*parsed);
    } else {
        // If we cannot parse, the snapshot still serves as a byte-faithful
        // backup; record only the raw hash and let the caller surface a
        // warning if needed.
        canonical_hash.clear();
    }

    SnapshotMetadata metadata;
    metadata.snapshot_schema_version = kSnapshotSchemaVersion;
    metadata.snapshot_id = snapshot_id;
    metadata.created_at = NowUtcString();
    metadata.created_by = created_by.empty() ? std::string("system") : created_by;
    metadata.reason = "Initial snapshot for event-store replay verification";
    metadata.transaction_sequence = 0;
    metadata.event_sequence = 0;
    metadata.raw_file_hash = raw_hash;
    metadata.canonical_model_hash = canonical_hash;

    if (!WriteSnapshotMetadata(project_root, metadata, error))
        return false;

    out_metadata = std::move(metadata);
    return true;
}

} // namespace core::audit
