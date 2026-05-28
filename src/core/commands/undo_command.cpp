#include "core/commands/undo_command.h"

namespace core::commands {

bool UndoLastTransactionCommand::Apply(CommandContext& ctx,
                                       audit::AuditEvent& out_event,
                                       std::string& out_error) {
    if (undone_transaction_sequence_ == 0) {
        out_error = "Cannot undo: no transaction sequence specified";
        return false;
    }

    // Wholesale-replace the live state with the reconstructed prior state.
    // This is the "honest audit trail" approach (Plan §5): we do not try
    // to generate an inverse event-set. Instead, the audit log carries an
    // explicit `Undo` marker, and the replayer (event_replayer.cpp) is
    // aware of these markers and skips the transactions they cancel.
    ctx.model   = std::move(prior_model_);
    ctx.package = std::move(prior_package_);

    out_event.event_type                          = "Undo";
    out_event.payload["undone_transaction_sequence"] = undone_transaction_sequence_;
    out_event.payload["undone_command_name"]         = undone_command_name_;
    return true;
}

} // namespace core::commands
