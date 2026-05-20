#include "core/commands/command_bus.h"

#include "core/audit/audit_paths.h"
#include "core/audit/canonical_model_hash.h"
#include "core/project_file_io.h"
#include "core/sha256.h"
#include "sacm/sacm_serializer.h"

#include <filesystem>

namespace core::commands {

std::unique_ptr<CommandBus> CommandBus::Open(AssuranceProject project,
                                             std::filesystem::path sacm_absolute_path,
                                             std::string& error) {
    auto bus = std::unique_ptr<CommandBus>(new CommandBus());
    bus->project_ = std::move(project);
    bus->sacm_path_ = std::move(sacm_absolute_path);

    if (!core::audit::ReadAuditManifest(bus->project_.rootPath, bus->manifest_, error))
        return nullptr;

    bus->store_ = core::audit::EventStore::Open(bus->project_.rootPath, error);
    if (!bus->store_)
        return nullptr;

    return bus;
}

CommandResult CommandBus::Execute(ICommand& command, CommandContext& ctx, const std::string& author) {
    CommandResult result;

    audit::AuditEvent event;
    std::string apply_error;
    if (!command.Apply(ctx, event, apply_error)) {
        result.success = false;
        result.error = std::move(apply_error);
        return result;
    }

    // Compute post-state hashes off the in-memory model, then serialize to
    // disk. We serialize once and hash the bytes we actually wrote so the
    // raw_file_hash recorded in the manifest is guaranteed to match the file
    // a reader will later open.
    const std::string canonical_after = audit::CanonicalModelHash(ctx.package);
    const std::string xml = sacm::serialize_sacm(ctx.package);

    auto write = WriteTextFile(sacm_path_, xml);
    if (!write) {
        result.success = false;
        result.error = "Autosave failed: " + write.error();
        return result;
    }
    const std::string raw_after = Sha256::HexDigest(xml);

    // Append a single-event transaction. EventStore::Append assigns the
    // sequences and previous_transaction_hash; we just supply the metadata.
    audit::AuditTransaction tx;
    tx.command_name = command.Name();
    tx.author = author.empty() ? std::string("system") : author;
    tx.events.push_back(std::move(event));

    std::string append_error;
    if (!store_->Append(tx, append_error)) {
        // Disk SACM is updated but the audit log entry failed. Surface the
        // error; the next project open will detect the hash mismatch and the
        // reconciliation flow (phase 5) will handle it.
        result.success = false;
        result.error = "Event log append failed: " + append_error;
        return result;
    }

    manifest_.latest_transaction_sequence = store_->LatestTransactionSequence();
    manifest_.latest_event_sequence = store_->LatestEventSequence();
    manifest_.event_store_hash = store_->EventStoreHash();
    manifest_.last_known_raw_file_hash = raw_after;
    manifest_.last_known_canonical_model_hash = canonical_after;

    std::string manifest_error;
    if (!audit::WriteAuditManifest(project_.rootPath, manifest_, manifest_error)) {
        // The transaction is committed; only the manifest summary is stale.
        // It will be regenerated on the next successful Execute. Surface a
        // soft warning by returning success with a non-fatal error string.
        result.success = true;
        result.error = "Manifest update failed (audit log is still consistent): " + manifest_error;
    } else {
        result.success = true;
    }

    result.transaction_id = tx.transaction_id;
    result.transaction_sequence = tx.transaction_sequence;
    result.raw_file_hash_after = raw_after;
    result.canonical_model_hash_after = canonical_after;
    return result;
}

} // namespace core::commands
