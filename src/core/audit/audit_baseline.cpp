#include "core/audit/audit_baseline.h"

#include "core/audit/audit_manifest.h"
#include "core/audit/audit_paths.h"
#include "core/project_file_io.h"
#include "core/time_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <system_error>

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

std::string MakeBaselineId(std::uint64_t transaction_sequence) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "baseline_%06llu",
                  static_cast<unsigned long long>(transaction_sequence));
    return buf;
}

} // namespace

std::string SerializeBaselineMetadata(const BaselineMetadata& m) {
    json j;
    j["baseline_schema_version"] = m.baseline_schema_version;
    j["baseline_id"] = m.baseline_id;
    j["name"] = m.name;
    j["description"] = m.description;
    j["created_at"] = m.created_at;
    j["created_by"] = m.created_by;
    j["transaction_sequence"] = m.transaction_sequence;
    j["canonical_model_hash"] = m.canonical_model_hash;
    return j.dump(2) + "\n";
}

bool ParseBaselineMetadata(const std::string& text, BaselineMetadata& out, std::string& error) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& ex) {
        error = std::string("Invalid baseline metadata JSON: ") + ex.what();
        return false;
    }
    if (!j.is_object()) {
        error = "Baseline metadata root is not a JSON object";
        return false;
    }
    out = BaselineMetadata{};
    out.baseline_schema_version =
        ReadOr<int>(j, "baseline_schema_version", kBaselineSchemaVersion);
    out.baseline_id = ReadOr<std::string>(j, "baseline_id", {});
    out.name = ReadOr<std::string>(j, "name", {});
    out.description = ReadOr<std::string>(j, "description", {});
    out.created_at = ReadOr<std::string>(j, "created_at", {});
    out.created_by = ReadOr<std::string>(j, "created_by", {});
    out.transaction_sequence = ReadOr<std::uint64_t>(j, "transaction_sequence", 0);
    out.canonical_model_hash = ReadOr<std::string>(j, "canonical_model_hash", {});
    return true;
}

bool ReadBaselineMetadata(const std::filesystem::path& project_root,
                          const std::string& baseline_id,
                          BaselineMetadata& out,
                          std::string& error) {
    auto text = ReadTextFile(BaselineMetadataPath(project_root, baseline_id));
    if (!text) {
        error = std::move(text.error());
        return false;
    }
    return ParseBaselineMetadata(*text, out, error);
}

bool WriteBaselineMetadata(const std::filesystem::path& project_root,
                           const BaselineMetadata& metadata,
                           std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(BaselinesDir(project_root), ec);
    if (ec) {
        error = "Could not create baselines directory: " + ec.message();
        return false;
    }
    auto r = WriteTextFileAtomic(BaselineMetadataPath(project_root, metadata.baseline_id),
                                 SerializeBaselineMetadata(metadata));
    if (!r) {
        error = std::move(r.error());
        return false;
    }
    return true;
}

std::vector<BaselineMetadata> ListBaselines(const std::filesystem::path& project_root,
                                            std::vector<std::string>* warnings_out) {
    std::vector<BaselineMetadata> result;
    AuditManifest manifest;
    std::string err;
    if (!ReadAuditManifest(project_root, manifest, err)) {
        if (warnings_out)
            warnings_out->push_back("Could not read manifest while listing baselines: " + err);
        return result;
    }
    result.reserve(manifest.baseline_ids.size());
    for (const std::string& id : manifest.baseline_ids) {
        BaselineMetadata md;
        std::string parse_err;
        if (!ReadBaselineMetadata(project_root, id, md, parse_err)) {
            if (warnings_out)
                warnings_out->push_back("Skipping baseline " + id + ": " + parse_err);
            continue;
        }
        result.push_back(std::move(md));
    }
    std::sort(result.begin(), result.end(), [](const BaselineMetadata& a, const BaselineMetadata& b) {
        return a.transaction_sequence < b.transaction_sequence;
    });
    return result;
}

bool CreateBaseline(const std::filesystem::path& project_root,
                    const CreateBaselineRequest& request,
                    BaselineMetadata& out_metadata,
                    std::string& error) {
    AuditManifest manifest;
    if (!ReadAuditManifest(project_root, manifest, error))
        return false;

    const std::uint64_t at_seq =
        request.at_transaction_sequence.value_or(manifest.latest_transaction_sequence);

    // Allocate a unique id. Collision can happen if two baselines pin to the
    // same sequence; in that case append `_<n>` until unique.
    std::string baseline_id = MakeBaselineId(at_seq);
    if (std::find(manifest.baseline_ids.begin(), manifest.baseline_ids.end(), baseline_id) !=
        manifest.baseline_ids.end()) {
        int suffix = 1;
        std::string candidate;
        do {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "_%d", suffix++);
            candidate = baseline_id + buf;
        } while (std::find(manifest.baseline_ids.begin(), manifest.baseline_ids.end(), candidate) !=
                 manifest.baseline_ids.end());
        baseline_id = std::move(candidate);
    }

    BaselineMetadata metadata;
    metadata.baseline_schema_version = kBaselineSchemaVersion;
    metadata.baseline_id = baseline_id;
    metadata.name = request.name;
    metadata.description = request.description;
    metadata.created_at = NowUtcString();
    metadata.created_by = request.created_by.empty() ? std::string("system") : request.created_by;
    metadata.transaction_sequence = at_seq;
    metadata.canonical_model_hash = request.canonical_model_hash;

    if (!WriteBaselineMetadata(project_root, metadata, error))
        return false;

    manifest.baseline_ids.push_back(baseline_id);
    if (!WriteAuditManifest(project_root, manifest, error)) {
        // Best-effort cleanup of the orphan sidecar so retries can succeed.
        std::error_code ec;
        std::filesystem::remove(BaselineMetadataPath(project_root, baseline_id), ec);
        return false;
    }

    out_metadata = std::move(metadata);
    return true;
}

} // namespace core::audit
