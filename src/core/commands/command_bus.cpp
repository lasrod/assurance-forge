#include "core/commands/command_bus.h"

#include "core/audit/audit_paths.h"
#include "core/audit/canonical_model_hash.h"
#include "core/library_package_projection.h"
#include "core/project_file_io.h"
#include "core/sha256.h"
#include "sacm_adapter/library_load.h"
#include "sacm/sacm_parser.h"
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

    // Serialize once so the bytes we write, the bytes we hash, and the
    // bytes we (notionally) re-parse for the canonical hash are identical.
    const std::string xml = sacm::serialize_sacm(ctx.package);
    const std::string raw_after = Sha256::HexDigest(xml);

    // Phase 9 Stage 6: derive the canonical hash through the library so this
    // manifest cache converges with the verifier's replayed and on-disk hashes,
    // which are also library-derived. Fall back to the legacy hash only if the
    // library cannot read our own serialization (a bug, not an expected path).
    std::string canonical_after;
    if (auto library_hash = core::library_canonical_hash_from_xml(xml)) {
        canonical_after = *library_hash;
    } else {
        canonical_after = audit::CanonicalModelHash(ctx.package);
    }

    // Step 1: append to the audit log FIRST and fsync. The log is the
    // canonical record of intent — if we crash after this point the next
    // open can re-derive SACM from replay. If we crashed *before* this
    // point, no committed state exists yet and the next open sees the
    // previous SACM unchanged.
    audit::AuditTransaction tx;
    tx.command_name = command.Name();
    tx.author = author.empty() ? std::string("system") : author;
    tx.events.push_back(std::move(event));

    std::string append_error;
    if (!store_->Append(tx, append_error)) {
        result.success = false;
        result.error = "Event log append failed: " + append_error;
        return result;
    }

    // From this point on the transaction is committed in the audit log and
    // the in-memory model has already been mutated. Any subsequent failure
    // (SACM write, manifest write) is a soft warning: we return success
    // so callers don't treat the command as failed and roll back UI
    // state, but we populate `error` with a user-visible diagnostic.
    // Crash recovery uses the audit log as the source of truth on next
    // open and detects the SACM/manifest gap via the verifier.
    result.transaction_id = tx.transaction_id;
    result.transaction_sequence = tx.transaction_sequence;
    result.raw_file_hash_after = raw_after;
    result.canonical_model_hash_after = canonical_after;
    result.success = true;

    // Phase 9 Stage 5 safety net: keep the library-owned document consistent
    // with the just-committed edit. A command that routed its edit through a
    // library operation set `library_synced` and already mutated it natively;
    // for anything else, re-derive it from the authoritative serialization
    // (`xml`, the same bytes we hashed) so it never drifts. This touches
    // neither the audit log nor the saved package, so the tamper chain is
    // unaffected.
    if (ctx.library_document != nullptr && !ctx.library_synced) {
        if (!sacm_adapter::reload_document(*ctx.library_document, xml)) {
            // Soft warning: the edit is committed and the saved package is
            // authoritative, but the library-backed view could not be
            // re-derived, so surface it rather than let it drift silently.
            result.error = "Library view re-derive failed (edit committed).";
        }
    }

    // Step 2: write SACM atomically (temp file + fsync + rename). A crash
    // here leaves the audit log ahead of SACM by one transaction; on next
    // open the verifier detects the gap and offers non-destructive
    // remediation (Restore from audit replay).
    auto write = WriteTextFileAtomic(sacm_path_, xml);
    if (!write) {
        result.error = "Autosave failed (audit log entry was committed): " + write.error();
        return result;
    }

    // Step 3: update manifest atomically. The manifest is a cache; if this
    // fails the next open rebuilds it from the log + SACM. We still
    // propagate the error so the UI can show a soft warning.
    manifest_.latest_transaction_sequence = store_->LatestTransactionSequence();
    manifest_.latest_event_sequence = store_->LatestEventSequence();
    manifest_.event_store_hash = store_->EventStoreHash();
    manifest_.last_known_raw_file_hash = raw_after;
    manifest_.last_known_canonical_model_hash = canonical_after;

    std::string manifest_error;
    if (!audit::WriteAuditManifest(project_.rootPath, manifest_, manifest_error)) {
        result.error = "Manifest update failed (audit log and SACM are consistent): " + manifest_error;
    }

    return result;
}

} // namespace core::commands
