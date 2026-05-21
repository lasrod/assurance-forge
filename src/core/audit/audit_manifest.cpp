#include "core/audit/audit_manifest.h"

#include "core/audit/audit_paths.h"
#include "core/project_file_io.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>

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

std::string SerializeAuditManifest(const AuditManifest& m) {
    json j;
    j["manifest_schema_version"] = m.manifest_schema_version;
    j["project_id"] = m.project_id;
    j["created_at"] = m.created_at;
    j["created_by"] = m.created_by;
    j["current_sacm"] = m.current_sacm;
    j["initial_snapshot_id"] = m.initial_snapshot_id;
    j["latest_transaction_sequence"] = m.latest_transaction_sequence;
    j["latest_event_sequence"] = m.latest_event_sequence;
    j["last_known_raw_file_hash"] = m.last_known_raw_file_hash;
    j["last_known_canonical_model_hash"] = m.last_known_canonical_model_hash;
    j["event_store_hash"] = m.event_store_hash;
    return j.dump(2) + "\n";
}

bool ParseAuditManifest(const std::string& text, AuditManifest& out, std::string& error) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& ex) {
        error = std::string("Invalid manifest JSON: ") + ex.what();
        return false;
    }
    if (!j.is_object()) {
        error = "Manifest root is not a JSON object";
        return false;
    }
    out = AuditManifest{};
    out.manifest_schema_version = ReadOr<int>(j, "manifest_schema_version", kManifestSchemaVersion);
    out.project_id = ReadOr<std::string>(j, "project_id", {});
    out.created_at = ReadOr<std::string>(j, "created_at", {});
    out.created_by = ReadOr<std::string>(j, "created_by", {});
    out.current_sacm = ReadOr<std::string>(j, "current_sacm", {});
    out.initial_snapshot_id = ReadOr<std::string>(j, "initial_snapshot_id", {});
    out.latest_transaction_sequence = ReadOr<std::uint64_t>(j, "latest_transaction_sequence", 0);
    out.latest_event_sequence = ReadOr<std::uint64_t>(j, "latest_event_sequence", 0);
    out.last_known_raw_file_hash = ReadOr<std::string>(j, "last_known_raw_file_hash", {});
    out.last_known_canonical_model_hash = ReadOr<std::string>(j, "last_known_canonical_model_hash", {});
    out.event_store_hash = ReadOr<std::string>(j, "event_store_hash", {});
    return true;
}

bool ReadAuditManifest(const std::filesystem::path& project_root, AuditManifest& out, std::string& error) {
    auto text = ReadTextFile(ManifestPath(project_root));
    if (!text) {
        error = std::move(text.error());
        return false;
    }
    return ParseAuditManifest(*text, out, error);
}

bool WriteAuditManifest(const std::filesystem::path& project_root,
                        const AuditManifest& manifest,
                        std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(AfDir(project_root), ec);
    if (ec) {
        error = "Could not create .af directory: " + ec.message();
        return false;
    }
    const std::string text = SerializeAuditManifest(manifest);
    auto r = WriteTextFileAtomic(ManifestPath(project_root), text);
    if (!r) {
        error = std::move(r.error());
        return false;
    }
    return true;
}

} // namespace core::audit
