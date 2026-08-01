#pragma once

#include "core/audit/audit_transaction.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// Undo resolver (Plan §5). Inspects a transaction log and computes:
//   - the set of transaction sequences that are effectively undone, and
//   - the next "live" transaction the user can target with Ctrl+Z.
//
// An `Undo` event carries `payload.undone_transaction_sequence = N`. A
// later `Undo` of the Undo transaction itself "redoes" N (cancels the
// cancellation). To resolve this correctly we iterate the log in REVERSE
// order: an Undo event is honored only if the transaction it lives in
// has not already been cancelled by a *later* Undo. This yields the same
// effective-state set as the live application sequence and works for
// arbitrary undo / redo chains.
namespace core::audit {

// Set of transaction sequences whose effects are cancelled by an active
// (i.e. not-itself-cancelled) Undo event.
std::unordered_set<std::uint64_t> ComputeUndoSkipSet(const std::vector<AuditTransaction>& transactions);

struct UndoTarget {
    bool has_target = false;
    std::uint64_t target_sequence = 0; // transaction the next Ctrl+Z would undo
    std::string target_command_name;   // for the audit event payload + UI hint
};

// Find the most recent transaction that is currently in-force and is not
// itself an Undo marker — i.e. the transaction whose effects the next
// Ctrl+Z should cancel. Returns `has_target=false` for an empty or
// fully-undone log.
UndoTarget FindUndoTarget(const std::vector<AuditTransaction>& transactions);

} // namespace core::audit
