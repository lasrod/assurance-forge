#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Baseline metadata. A baseline is a named, lightweight pointer to a specific
// audit-store transaction sequence. Unlike snapshots, baselines do NOT copy
// the SACM bytes; they only record where in history a meaningful checkpoint
// occurred (e.g. "released to assessor", "v1.2 review submission"). Baselines
// participate in the Assurance Timeline rail as major markers.
//
// Storage layout (on disk):
//   <project_root>/.af/baselines/baseline_<id>.json
//
// The manifest carries an index of baseline ids in `AuditManifest::baseline_ids`
// so listings stay fast without scanning the directory.
namespace core::audit {

inline constexpr int kBaselineSchemaVersion = 1;

struct BaselineMetadata {
    int           baseline_schema_version = kBaselineSchemaVersion;
    // Stable identifier. Recommended format: `baseline_<6-digit-zero-padded-seq>`,
    // e.g. `baseline_000042`. Must be unique within the project.
    std::string   baseline_id;
    std::string   name;
    std::string   description;
    std::string   created_at;
    std::string   created_by;
    // Transaction sequence the baseline pins to. 0 means "initial snapshot
    // before any transactions".
    std::uint64_t transaction_sequence = 0;
    // Optional canonical model hash captured at baseline-creation time. May be
    // empty when the reconstructed model is unavailable.
    std::string   canonical_model_hash;
};

std::string SerializeBaselineMetadata(const BaselineMetadata& metadata);
bool        ParseBaselineMetadata(const std::string& text, BaselineMetadata& out, std::string& error);

bool ReadBaselineMetadata(const std::filesystem::path& project_root,
                          const std::string& baseline_id,
                          BaselineMetadata& out,
                          std::string& error);
bool WriteBaselineMetadata(const std::filesystem::path& project_root,
                           const BaselineMetadata& metadata,
                           std::string& error);

// Enumerate every baseline currently registered in the manifest. Reads each
// sidecar file; baselines whose sidecar fails to parse are skipped (and
// reported in `warnings_out`). Returns baselines sorted by
// `transaction_sequence` ascending.
std::vector<BaselineMetadata> ListBaselines(const std::filesystem::path& project_root,
                                            std::vector<std::string>* warnings_out = nullptr);

struct CreateBaselineRequest {
    std::string                  name;
    std::string                  description;
    std::string                  created_by;
    // Transaction sequence to pin the baseline to. When std::nullopt, the
    // implementation defaults to the manifest's `latest_transaction_sequence`.
    std::optional<std::uint64_t> at_transaction_sequence;
    // Optional canonical model hash to embed (caller may supply when it
    // already has the reconstructed state).
    std::string                  canonical_model_hash;
};

// Create a new baseline: writes the sidecar file, appends the id to the
// manifest's `baseline_ids`, and rewrites the manifest atomically. Fails when
// the project has no manifest, when a baseline with the generated id already
// exists, or on any I/O error.
bool CreateBaseline(const std::filesystem::path& project_root,
                    const CreateBaselineRequest& request,
                    BaselineMetadata& out_metadata,
                    std::string& error);

} // namespace core::audit
