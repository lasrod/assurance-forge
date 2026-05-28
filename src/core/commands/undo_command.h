#pragma once

#include "core/commands/command_bus.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <cstdint>
#include <string>

// Undo command (Plan §5.3). Atomically replaces the live model and SACM
// package with a previously-reconstructed prior state and appends a single
// `Undo` audit event describing what was undone.
//
// The command does NOT compute the prior state itself — the orchestrator
// (typically `AppRuntime::Undo()` via `app::commands::DispatchAuditedCommand`)
// is responsible for calling `core::audit::ReconstructAtSequence` and
// passing the reconstructed model + package in. This keeps the command
// pure (no I/O) and matches the rest of the command bus's contract.
namespace core::commands {

class UndoLastTransactionCommand final : public ICommand {
public:
    UndoLastTransactionCommand(std::uint64_t              undone_transaction_sequence,
                               std::string                undone_command_name,
                               parser::AssuranceCase      prior_model,
                               sacm::AssuranceCasePackage prior_package)
        : undone_transaction_sequence_(undone_transaction_sequence),
          undone_command_name_(std::move(undone_command_name)),
          prior_model_(std::move(prior_model)),
          prior_package_(std::move(prior_package)) {}

    std::string Name() const override { return "Undo"; }

    bool Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) override;

    std::uint64_t       UndoneTransactionSequence() const { return undone_transaction_sequence_; }
    const std::string&  UndoneCommandName() const { return undone_command_name_; }

private:
    std::uint64_t              undone_transaction_sequence_;
    std::string                undone_command_name_;
    parser::AssuranceCase      prior_model_;
    sacm::AssuranceCasePackage prior_package_;
};

} // namespace core::commands
