#pragma once

#include "core/audit/audit_transaction.h"

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

} // namespace core::audit
