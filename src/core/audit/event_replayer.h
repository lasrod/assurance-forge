#pragma once

#include "core/audit/audit_transaction.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// Audit replay engine (design §11). Given the initial snapshot of a project
// and the full chain of audit transactions, the replayer reconstructs the
// current model state by applying every event in order, using the same
// `core::*` mutators as the live command bus.
//
// Replay must be deterministic: every identifier the command bus generated
// at the time the event was originally produced is carried in the event
// payload and re-applied verbatim. The replayer therefore never calls the
// id-generating overloads of `core::AddChildElement` / `core::AddTopGoal`;
// it routes through `core::AddChildElementWithIds` / `core::AddTopGoalWithId`
// instead. Removes are deterministic by construction (they take an existing
// element id and a removal mode).
namespace core::audit {

struct ReplayState {
    parser::AssuranceCase       model;
    sacm::AssuranceCasePackage  package;
};

class Replayer {
public:
    // Replay every event in `transactions` (in transaction-sequence then
    // event-sequence order — the same order they were appended) on top of
    // the supplied snapshot state. Events with a `transaction_sequence`
    // strictly greater than `up_to_transaction_sequence` are skipped, so
    // passing `UINT64_MAX` replays the entire log.
    //
    // `from_transaction_sequence` is an EXCLUSIVE lower bound: events with a
    // `transaction_sequence` less than or equal to it are skipped, because the
    // supplied snapshot is a *trusted replay root* that already reflects them.
    // The default (0) replays from the very beginning (sequences start at 1),
    // matching the "snapshot 0 + full log" behaviour. When replaying from a
    // trusted baseline at sequence N, pass `from_transaction_sequence = N`.
    // Undo events are resolved over the replayed window `(from, up_to]` only;
    // an undo cannot cross a trusted baseline (the baseline has already baked
    // in every transaction at or before it).
    //
    // Returns the reconstructed state on success. On the first event that
    // fails to apply, returns an error string containing the offending
    // transaction / event sequence and the underlying mutator error so the
    // verifier can surface meaningful diagnostics.
    static std::expected<ReplayState, std::string> ReplayFrom(
        const parser::AssuranceCase&         snapshot_model,
        const sacm::AssuranceCasePackage&    snapshot_package,
        const std::vector<AuditTransaction>& transactions,
        std::uint64_t                        up_to_transaction_sequence,
        std::uint64_t                        from_transaction_sequence = 0);
};

} // namespace core::audit
