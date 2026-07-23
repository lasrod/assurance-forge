#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// On-disk manifest for the audit / event-store subsystem (design §6).
// Stored at `<project_root>/.af/manifest.af.json`.
//
// Schema is intentionally permissive — readers ignore unknown fields and
// writers always emit `manifest_schema_version` so we can evolve the format
// in a forward-compatible way.
namespace core::audit {

inline constexpr int kManifestSchemaVersion = 1;

struct AuditManifest {
    int         manifest_schema_version = kManifestSchemaVersion;
    std::string project_id;
    std::string created_at;
    std::string created_by;
    // Path of the materialized SACM file the event store applies to, relative
    // to the project root.
    std::string current_sacm;
    std::string initial_snapshot_id;
    // The trusted replay root: the snapshot the verifier and recovery replay
    // forward from, applying only events after that snapshot's transaction
    // sequence. Empty means "replay from `initial_snapshot_id`" (snapshot 0 +
    // the full log), which is the default for every project until a trusted
    // baseline is promoted (e.g. after a migration). Kept SEPARATE from
    // `initial_snapshot_id` so the timeline still tags snapshot 0 as the
    // initial state and the history slider still reconstructs early states.
    std::string replay_root_snapshot_id;
    std::uint64_t latest_transaction_sequence = 0;
    std::uint64_t latest_event_sequence = 0;
    std::string last_known_raw_file_hash;
    std::string last_known_canonical_model_hash;
    // sha256 of the on-disk event log (transactions.af.jsonl) at the time the
    // manifest was last written. Empty when the log is empty.
    std::string event_store_hash;
    // Index of baselines registered under `<project_root>/.af/baselines/`.
    // Each id corresponds to a sidecar JSON file; see `audit_baseline.h`.
    // Ordered chronologically (creation order). Forward-compatible: older
    // manifests without this field parse cleanly as an empty list.
    std::vector<std::string> baseline_ids;
};

// Serialize to pretty JSON text suitable for direct file write.
std::string SerializeAuditManifest(const AuditManifest& manifest);

// Parse JSON text. On failure populates `error` and returns false.
bool ParseAuditManifest(const std::string& text, AuditManifest& out, std::string& error);

// Convenience: read/write `<project_root>/.af/manifest.af.json`.
bool ReadAuditManifest(const std::filesystem::path& project_root, AuditManifest& out, std::string& error);
bool WriteAuditManifest(const std::filesystem::path& project_root, const AuditManifest& manifest, std::string& error);

} // namespace core::audit
