#include "core/audit/audit_accept.h"

#include "core/audit/audit_snapshot.h"
#include "core/library_package_projection.h"
#include "core/project_file_io.h"
#include "core/sha256.h"

#include <optional>
#include <system_error>
#include <utility>

namespace core::audit {

namespace {

std::string RelativeSacmPath(const std::filesystem::path& project_root, const std::filesystem::path& sacm_absolute) {
    std::error_code relative_error;
    const std::filesystem::path relative = std::filesystem::relative(sacm_absolute, project_root, relative_error);
    if (relative_error || relative.empty())
        return sacm_absolute.generic_string();
    return relative.generic_string();
}

} // namespace

std::string AcceptedDocumentFromEvent(const AuditEvent& event) {
    const auto document = event.payload.find("document");
    if (document == event.payload.end() || !document->is_string())
        return {};
    return document->get<std::string>();
}

bool RecordAcceptedDocument(const std::filesystem::path& project_root,
                            const std::filesystem::path& sacm_absolute_path,
                            EventStore& store,
                            AuditManifest& manifest,
                            const std::string& author,
                            const DraftPromotionRecord& provenance,
                            RecordAcceptedDocumentResult& out_result,
                            std::string& error) {
    out_result = RecordAcceptedDocumentResult{};
    error.clear();

    // The bytes the accept wrote, hashed the same two ways the command bus
    // hashes what it writes, so the manifest cache and the verifier's on-disk
    // reading agree about this file.
    const std::expected<std::vector<unsigned char>, std::string> bytes = ReadFileBytes(sacm_absolute_path);
    if (!bytes.has_value()) {
        error = "The accepted argument could not be read back for the audit log: " + bytes.error();
        return false;
    }
    const std::string xml(bytes->begin(), bytes->end());
    const std::optional<std::string> canonical = core::library_canonical_hash_from_xml(xml);
    if (!canonical.has_value()) {
        error = "The accepted argument could not be read through the SACM library, so its audit hash cannot be "
                "computed.";
        return false;
    }
    const std::string raw = Sha256::HexDigest(*bytes);

    // The document travels in the event. This is what makes the accept
    // replayable: a reader of the log a year from now can reproduce the accepted
    // argument without the draft, which the accept consumed.
    AuditEvent event;
    event.event_type = kWorkingDraftAcceptedEventType;
    event.payload = {
        {"sacm_path", RelativeSacmPath(project_root, sacm_absolute_path)},
        {"document", xml},
        {"raw_file_hash", raw},
        {"canonical_model_hash", *canonical},
    };

    AuditTransaction tx;
    tx.command_name = kAcceptWorkingDraftCommandName;
    tx.author = author.empty() ? std::string("system") : author;
    tx.draft_promotion = provenance;
    tx.events.push_back(std::move(event));

    std::string append_error;
    if (!store.Append(tx, append_error)) {
        error = "The audit log could not record the accept: " + append_error;
        return false;
    }
    out_result.transaction_sequence = tx.transaction_sequence;
    out_result.raw_file_hash = raw;
    out_result.canonical_model_hash = *canonical;

    // From here the accept is in the log. What follows is cache and root
    // maintenance; a failure is reported so the user knows, and the next open
    // rebuilds the manifest from the log it can now replay.
    manifest.latest_transaction_sequence = store.LatestTransactionSequence();
    manifest.latest_event_sequence = store.LatestEventSequence();
    manifest.event_store_hash = store.EventStoreHash();
    manifest.last_known_raw_file_hash = raw;
    manifest.last_known_canonical_model_hash = *canonical;
    std::string manifest_error;
    if (!WriteAuditManifest(project_root, manifest, manifest_error)) {
        out_result.warning = "The accept was recorded, but the audit manifest could not be updated: " + manifest_error;
        return true;
    }

    // The snapshot reads the manifest just written, so its id is this accept's
    // sequence and its bytes are the accepted file.
    SnapshotMetadata snapshot;
    std::string snapshot_error;
    if (!CreateUserSnapshot(project_root, "Working draft accepted", tx.author, snapshot, snapshot_error)) {
        out_result.warning = "The accept was recorded, but its snapshot could not be written: " + snapshot_error;
        return true;
    }
    out_result.snapshot_id = snapshot.snapshot_id;

    manifest.replay_root_snapshot_id = snapshot.snapshot_id;
    if (!WriteAuditManifest(project_root, manifest, manifest_error)) {
        out_result.warning =
            "The accept was recorded and snapshotted, but the manifest could not name the snapshot as the replay "
            "root: " +
            manifest_error;
    }
    return true;
}

} // namespace core::audit
