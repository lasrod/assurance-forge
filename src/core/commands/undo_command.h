#pragma once

#include "core/commands/command_bus.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"
#include "sacm_adapter/library_load.h"

#include <cstdint>
#include <memory>
#include <string>

// Undo command (Plan §5.3). Atomically replaces the live state with a
// previously-reconstructed prior state and appends a single `Undo` audit event
// describing what was undone.
//
// The command does NOT compute the prior state itself — the orchestrator
// (typically `AppRuntime::Undo()` via `app::commands::DispatchAuditedCommand`)
// is responsible for calling `core::audit::ReconstructAtSequence` and passing
// the reconstructed state in. This keeps the command pure (no I/O) and matches
// the rest of the command bus's contract.
//
// Two routings, same as every other command on the bus:
//
//   * LIBRARY-PRIMARY (a `prior_document` was reconstructed and the context has
//     a library document): the prior document BECOMES the live one and the bus
//     serializes it directly. This is the only routing that preserves what no
//     projection carries — unknown/foreign XML and vendor attributes — because
//     the reconstructed document already holds them: it was replayed from the
//     snapshot through the library, not projected out of a POD.
//   * LEGACY (no library document): the reconstructed `model`/`package` are
//     assigned over the live ones, and the bus serializes a projection.
//
// Undo applies whenever a document is present, like every other command.
// That switch exists to stop a command replacing the live views wholesale
// mid-dispatch, and for undo the two routings are inverted: the library-primary
// path leaves the views for the frame-boundary re-derive, while the legacy path
// is the one that replaces them. See the comment on the branch in undo_command.cpp.
namespace core::commands {

class UndoLastTransactionCommand final : public ICommand {
public:
    UndoLastTransactionCommand(std::uint64_t undone_transaction_sequence,
                               std::string undone_command_name,
                               parser::AssuranceCase prior_model,
                               sacm::AssuranceCasePackage prior_package,
                               std::unique_ptr<sacm_adapter::LibraryDocument> prior_document = nullptr)
        : undone_transaction_sequence_(undone_transaction_sequence),
          undone_command_name_(std::move(undone_command_name)),
          prior_model_(std::move(prior_model)),
          prior_package_(std::move(prior_package)),
          prior_document_(std::move(prior_document)) {}

    std::string Name() const override {
        return "Undo";
    }

    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    std::uint64_t UndoneTransactionSequence() const {
        return undone_transaction_sequence_;
    }
    const std::string& UndoneCommandName() const {
        return undone_command_name_;
    }

private:
    std::uint64_t undone_transaction_sequence_;
    std::string undone_command_name_;
    parser::AssuranceCase prior_model_;
    sacm::AssuranceCasePackage prior_package_;
    std::unique_ptr<sacm_adapter::LibraryDocument> prior_document_;
};

} // namespace core::commands
