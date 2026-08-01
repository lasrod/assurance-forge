#include "core/audit/undo_resolver.h"

namespace core::audit {

namespace {

// Returns the undone-tx-sequence carried by `event`, or 0 if it's not a
// valid Undo marker. 0 is safe as a sentinel: real undo events always
// reference a non-zero sequence (transaction sequences start at 1).
std::uint64_t ExtractUndoneSequence(const AuditEvent& event) {
    if (event.event_type != "Undo")
        return 0;
    auto it = event.payload.find("undone_transaction_sequence");
    if (it == event.payload.end() || !it->is_number_unsigned())
        return 0;
    return it->get<std::uint64_t>();
}

bool TransactionIsPureUndo(const AuditTransaction& tx) {
    if (tx.events.empty())
        return false;
    for (const AuditEvent& e : tx.events) {
        if (e.event_type != "Undo")
            return false;
    }
    return true;
}

} // namespace

std::unordered_set<std::uint64_t> ComputeUndoSkipSet(const std::vector<AuditTransaction>& transactions) {
    std::unordered_set<std::uint64_t> skipped;

    // Reverse iteration: an Undo only takes effect if the transaction
    // carrying it has not itself been cancelled by a strictly later Undo.
    for (auto it = transactions.rbegin(); it != transactions.rend(); ++it) {
        const AuditTransaction& tx = *it;
        if (skipped.count(tx.transaction_sequence) != 0)
            continue;
        for (const AuditEvent& event : tx.events) {
            const std::uint64_t target = ExtractUndoneSequence(event);
            if (target != 0)
                skipped.insert(target);
        }
    }
    return skipped;
}

UndoTarget FindUndoTarget(const std::vector<AuditTransaction>& transactions) {
    const auto skipped = ComputeUndoSkipSet(transactions);

    UndoTarget result;
    for (auto it = transactions.rbegin(); it != transactions.rend(); ++it) {
        const AuditTransaction& tx = *it;
        if (skipped.count(tx.transaction_sequence) != 0)
            continue;
        if (TransactionIsPureUndo(tx))
            continue;
        result.has_target = true;
        result.target_sequence = tx.transaction_sequence;
        result.target_command_name = tx.command_name;
        return result;
    }
    return result;
}

} // namespace core::audit
