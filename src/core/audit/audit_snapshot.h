#pragma once

#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"

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
    int snapshot_schema_version = kSnapshotSchemaVersion;
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

// Create a user-initiated snapshot of the project's current SACM file.
//
// Reads `<project_root>/.af/manifest.af.json` to locate the materialized
// SACM file (`current_sacm`) and the latest transaction/event sequences,
// then writes a new snapshot directory at
// `<project_root>/.af/snapshots/<snapshot_id>/` containing a verbatim copy
// of the SACM bytes plus a metadata sidecar. `snapshot_id` is derived from
// the current transaction sequence (zero-padded). The function fails when a
// snapshot with that id already exists, when the manifest cannot be read,
// or on any I/O error.
bool CreateUserSnapshot(const std::filesystem::path& project_root,
                        const std::string& reason,
                        const std::string& created_by,
                        SnapshotMetadata& out_metadata,
                        std::string& error);

// The trusted replay root a project should replay forward from.
struct ReplayRoot {
    std::string snapshot_id;                     // the snapshot to load as the base state
    std::uint64_t from_transaction_sequence = 0; // exclusive floor: replay events after this
};

// Resolve the trusted replay root. Prefers `replay_root_snapshot_id` (a promoted
// trusted baseline); when empty, falls back to `initial_snapshot_id` (snapshot 0,
// floor 0 = replay the full log). The floor is the root snapshot's own
// transaction sequence. If the chosen root's metadata cannot be read, falls back
// to the initial snapshot with floor 0 -- never trust a floor we cannot verify,
// so the worst case is replaying the entire log (today's behaviour).
ReplayRoot ResolveReplayRoot(const std::filesystem::path& project_root,
                             const std::string& replay_root_snapshot_id,
                             const std::string& initial_snapshot_id);

// Load a snapshot SACM file into the legacy (model, package) pair, preferring the
// SACM library so a library-XMI snapshot -- which the legacy `sacm::parse_sacm`
// reads near-empty -- loads with full content. Falls back to the legacy parsers
// only when the library cannot read the file, so a legacy-XML snapshot still
// loads. Returns false with `error` set only when both paths fail. Shared by the
// verifier, recovery, and history snapshot loaders (Phase 1b).
bool LoadSnapshotModels(const std::filesystem::path& sacm_path,
                        parser::AssuranceCase& out_model,
                        sacm::AssuranceCasePackage& out_package,
                        std::string& error);

} // namespace core::audit
