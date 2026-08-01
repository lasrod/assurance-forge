#include "core/commands/undo_command.h"

namespace core::commands {

bool UndoLastTransactionCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    if (undone_transaction_sequence_ == 0) {
        out_error = "Cannot undo: no transaction sequence specified";
        return false;
    }

    // Wholesale-replace the live state with the reconstructed prior state.
    // This is the "honest audit trail" approach (Plan §5): we do not try
    // to generate an inverse event-set. Instead, the audit log carries an
    // explicit `Undo` marker, and the replayer (event_replayer.cpp) is
    // aware of these markers and skips the transactions they cancel.
    // Deliberately NOT gated on `ctx.allow_library_primary`, unlike every other
    // flipped command. That kill switch exists to stop a command replacing the
    // live views wholesale mid-dispatch; for undo the routings are INVERTED --
    // the library-primary path leaves the views alone and the legacy path is the
    // one that replaces them. Honouring the switch here would therefore buy the
    // hazard it exists to prevent, and pay for it twice: the legacy path also
    // restores the WRONG preserved content, because the Stage-5 net re-derives
    // the document from projection bytes and adopts the compatibility content of
    // the outgoing (post-edit) document rather than of the state being restored.
    if (ctx.library_document != nullptr && prior_document_ != nullptr) {
        // The reconstructed document BECOMES the live one. Move-assigning the
        // contained document keeps the wrapper's identity, so `AppState`'s
        // `unique_ptr` and every raw pointer into it (including `ctx`'s) stay
        // valid.
        *ctx.library_document = std::move(*prior_document_);
        prior_document_.reset();
        ctx.library_primary = true;

        // Deliberately NOT assigning ctx.model / ctx.package. A library-primary
        // command leaves the live views for the caller's frame-boundary
        // re-derive (`AppRuntime::RebuildDerivedViewsIfNeeded`), because
        // replacing them mid-dispatch frees containers the canvas is still
        // rendering from this frame -- the hazard the bus stopped taking for
        // every other flipped command, which undo was still taking here.
    } else {
        ctx.model = std::move(prior_model_);
        ctx.package = std::move(prior_package_);
    }

    out_event.event_type = "Undo";
    out_event.payload["undone_transaction_sequence"] = undone_transaction_sequence_;
    out_event.payload["undone_command_name"] = undone_command_name_;
    return true;
}

} // namespace core::commands
