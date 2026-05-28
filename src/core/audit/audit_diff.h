#pragma once

#include "core/audit/audit_transaction.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

// Per-transaction change-set computation (design §16.3). Given an audit
// transaction, derive the sets of element ids that were added, modified and
// deleted by the events in that transaction.
//
// Used by the History Timeline UI to highlight which canvas nodes a given
// transaction touched, and by future per-element history filters.
//
// Phase 4a scope: covers the event types produced by Phase 2's element
// commands (CreateTopGoal, CreateChildElement, RemoveElement). Unknown event
// types are tolerated and contribute nothing — readers ignore them so the
// log remains forward-compatible.
namespace core::audit {

struct AuditChangeSet {
    std::unordered_set<std::string> added;
    std::unordered_set<std::string> modified;
    std::unordered_set<std::string> deleted;
};

AuditChangeSet ComputeChangeSet(const AuditTransaction& tx);

// Aggregate the change-sets of every transaction in [first, last] (inclusive,
// in transaction-sequence order). Useful for "what changed between snapshot N
// and the present" style queries.
AuditChangeSet ComputeChangeSet(const std::vector<AuditTransaction>& transactions);

// Per-element history summary derived from a transaction log. Used by the
// Element Properties panel to surface "Last changed", "Changes", and
// "Changed since baseline" lifecycle info.
struct ElementHistorySummary {
    bool          exists = false;                  // false if last touch was a delete
    bool          ever_seen = false;               // true if element appeared in any transaction
    std::uint64_t change_count = 0;                // adds + modifies + deletes touching the element
    std::uint64_t first_sequence = 0;              // sequence of the first transaction that added it
    std::uint64_t last_sequence = 0;               // sequence of the last transaction that touched it
    std::string   created_at;                      // timestamp of first add (ISO-8601)
    std::string   created_by;                      // author of first add
    std::string   last_changed_at;                 // timestamp of last touching transaction
    std::string   last_changed_by;                 // author of last touching transaction
    bool          changed_since_baseline = false;  // true if any change at seq > baseline_sequence
};

// Compute a per-element history summary from a transaction log. When
// `baseline_sequence` is provided, sets `changed_since_baseline` to true if
// the element was added, modified, or deleted in any transaction with a
// sequence strictly greater than `baseline_sequence`.
ElementHistorySummary SummarizeElementHistory(const std::string& element_id,
                                              const std::vector<AuditTransaction>& transactions,
                                              std::optional<std::uint64_t> baseline_sequence = std::nullopt);

} // namespace core::audit
