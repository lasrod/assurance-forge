#include "core/commands/command_bus.h"

#include "core/audit/audit_paths.h"
#include "core/audit/canonical_model_hash.h"
#include "core/derived_views.h"
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

    // Each command must opt into its edit-routing explicitly. The context is
    // reused across commands, so reset the per-command flags before `Apply`:
    // otherwise a `library_primary` flag left set by a PRIOR flipped command
    // would make the bus rebuild the views from the library over a LATER
    // unflipped command's legacy edit (losing it, and drifting the library).
    ctx.library_primary = false;
    ctx.library_synced = false;

    audit::AuditEvent event;
    std::string apply_error;
    if (!command.Apply(ctx, event, apply_error)) {
        // A library-primary command that failed part-way through a compound
        // mutation may already have changed the library. We do NOT rebuild the
        // live ctx.model/ctx.package here: replacing them wholesale mid-dispatch
        // frees containers the UI is still rendering from this frame (the canvas
        // holds &loaded_case across the frame). The caller re-derives the live
        // views from the library at the next frame boundary whenever the flip
        // engaged (ctx.library_primary), which also covers this partial-failure
        // case. See app::commands::DispatchAuditedCommand + RebuildDerivedViewsIfNeeded.
        result.success = false;
        result.error = std::move(apply_error);
        return result;
    }

    // Phase 2 slice 2b-1 -- the live edit flip. The command mutated the LIBRARY
    // natively, so the library is the source of truth for this edit. We derive the
    // package to serialize and hash from it -- but into a SCRATCH copy, never the
    // live ctx.model/ctx.package. Rebuilding those wholesale here frees containers
    // the UI is still rendering from this frame (the canvas holds &loaded_case
    // across the frame; a context-menu edit dispatches mid-render, so an in-dispatch
    // replace is a use-after-free). The caller re-derives the live views from the
    // library at the next frame boundary (AppRuntime::RebuildDerivedViewsIfNeeded),
    // the same safe point every other derived-view refresh uses. For an UNFLIPPED
    // command `source_package` is just the authoritative, in-place-mutated legacy
    // ctx.package, so serialization/hashing/autosave below are untouched.
    parser::AssuranceCase      flipped_scratch_model;
    sacm::AssuranceCasePackage flipped_scratch_package;
    const bool library_primary_flip = ctx.library_primary && ctx.library_document != nullptr;
    if (library_primary_flip)
        core::RebuildDerivedViewsFromLibrary(*ctx.library_document, flipped_scratch_model,
                                             flipped_scratch_package);
    sacm::AssuranceCasePackage& source_package = library_primary_flip ? flipped_scratch_package : ctx.package;

    // Serialize once so the bytes we write, the bytes we hash, and the
    // bytes we re-derive the canonical hash from are identical. Phase 9 Stage
    // 6: the library is now the serialization source of truth, so we write
    // library SACM XMI. The audit readers are already routed through the
    // library, so they read this back and converge. A failure to produce XMI
    // (the library could not read our own serialization -- a bug, not an
    // expected path) falls back to the legacy bytes but is surfaced as a soft
    // warning below rather than swallowed.
    //
    // Phase 9 Stage 7: when the flip engaged, serialize the LIBRARY DOCUMENT
    // directly rather than the package projected from it. The library already
    // holds the committed edit, so this is the same logical state the projection
    // described -- but it additionally preserves the unknown/foreign content the
    // legacy structs cannot model and therefore silently dropped on every save.
    // This is gated on the flip, NOT merely on `library_document != nullptr`:
    // an unflipped command (undo, a NodeOnly removal, an unsupported seam) mutated
    // the legacy package in place and the library does not hold that edit until
    // the Stage 5 net re-derives it from `xml` further down -- serializing the
    // document here would write the PRE-edit state.
    std::string xml;
    bool library_save_fell_back = false;
    bool lossy_document_fallback = false;
    bool serialized_from_library = false;
    if (library_primary_flip) {
        sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*ctx.library_document);
        if (saved.ok) {
            xml = std::move(saved.xml);
            serialized_from_library = true;
        } else {
            // The document could not be serialized, so we fall back to the package
            // projection below -- which is LOSSY for the unknown/foreign content
            // only the document holds. Surface it rather than silently degrading
            // the very preservation this path exists for.
            lossy_document_fallback = true;
        }
    }
    if (!serialized_from_library) {
        if (auto library_xml = core::library_xmi_from_package(source_package)) {
            xml = std::move(*library_xml);
        } else {
            xml = sacm::serialize_sacm(source_package);
            library_save_fell_back = true;
        }
    }
    const std::string raw_after = Sha256::HexDigest(xml);

    // Phase 9 Stage 6: derive the canonical hash through the library so this
    // manifest cache converges with the verifier's replayed and on-disk hashes,
    // which are also library-derived. Fall back to the legacy hash only if the
    // library cannot read our own serialization (a bug, not an expected path).
    std::string canonical_after;
    if (auto library_hash = core::library_canonical_hash_from_xml(xml)) {
        canonical_after = *library_hash;
    } else {
        canonical_after = audit::CanonicalModelHash(source_package);
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

    // Surface the Stage 6 library-XMI fallback (see above): the edit committed
    // and the audit stays consistent (the legacy bytes read back through the
    // library), but the saved file is legacy XML rather than XMI, which the
    // caller should know about.
    if (library_save_fell_back && result.error.empty()) {
        result.error = "Library XMI save failed; wrote the legacy serialization "
                       "(audit remains consistent).";
    } else if (lossy_document_fallback && result.error.empty()) {
        result.error = "Could not serialize the SACM library document; saved a projection "
                       "instead, which does not preserve unknown or vendor-specific content.";
    }

    // Phase 9 Stage 5 safety net: keep the library-owned document consistent
    // with the just-committed edit. A command that routed its edit through a
    // library operation set `library_synced` and already mutated it natively;
    // for anything else, re-derive it from the authoritative serialization
    // (`xml`, the same bytes we hashed) so it never drifts. This touches
    // neither the audit log nor the saved package, so the tamper chain is
    // unaffected.
    //
    // A `library_primary` command runs the OPPOSITE direction (the views were
    // rebuilt from the library above), so it must be excluded here or the net
    // would immediately overwrite the library with a package projected from it.
    //
    // Preserving reload: `xml` is a projection of the document being replaced,
    // and the legacy package cannot carry unknown/foreign XML, so the plain
    // reload dropped preserved vendor content on every unflipped command
    // (NodeOnly removal, undo, an unseamed command) -- from the in-memory
    // document and, through the next save, from disk.
    if (ctx.library_document != nullptr && !ctx.library_synced && !ctx.library_primary) {
        if (!sacm_adapter::reload_document_keeping_compatibility_content(*ctx.library_document,
                                                                         xml) &&
            result.error.empty()) {
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
