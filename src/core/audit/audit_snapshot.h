#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// Snapshot metadata (design §7). A snapshot is a full saved copy of a SACM
// file together with the metadata needed to replay events forward from that
// state. Snapshot 0 is created automatically when an audit store is
// initialized.
namespace core::audit {

inline constexpr int kSnapshotSchemaVersion = 1;

struct SnapshotMetadata {
    int         snapshot_schema_version = kSnapshotSchemaVersion;
    std::string snapshot_id;
    std::string created_at;
    std::string created_by;
    std::string reason;
    std::uint64_t transaction_sequence = 0;
    std::uint64_t event_sequence = 0;
    std::string raw_file_hash;
    std::string canonical_model_hash;
};

std::string SerializeSnapshotMetadata(const SnapshotMetadata& metadata);
bool ParseSnapshotMetadata(const std::string& text, SnapshotMetadata& out, std::string& error);

bool ReadSnapshotMetadata(const std::filesystem::path& project_root,
                          const std::string& snapshot_id,
                          SnapshotMetadata& out,
                          std::string& error);
bool WriteSnapshotMetadata(const std::filesystem::path& project_root,
                           const SnapshotMetadata& metadata,
                           std::string& error);

// Create the initial snapshot (`snapshot_000000`) from the given source SACM
// file: copies the file bytes into the snapshot directory verbatim and writes
// the metadata sidecar. Computes raw and canonical hashes from the copied
// content. Returns false on I/O or parse failure with `error` populated.
//
// Idempotent: if the snapshot already exists this returns true and leaves
// existing files untouched.
bool CreateInitialSnapshot(const std::filesystem::path& project_root,
                           const std::filesystem::path& source_sacm_absolute_path,
                           const std::string& created_by,
                           SnapshotMetadata& out_metadata,
                           std::string& error);

} // namespace core::audit
