#pragma once

#include "core/audit/audit_event.h"
#include "core/audit/audit_transaction.h"
#include "legacy_sacm/sacm_model.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Per-transaction package attribution. The audit log records mutations
// against the whole `AssuranceCasePackage`, but the GSN canvas filters to
// one `ArgumentPackage` at a time. These helpers let the History Timeline
// UI determine which transactions touched a given ArgumentPackage so it can
// render a per-package timeline without changing the on-disk schema.
//
// The scope of a package evolves as transactions are applied (creations
// extend it, deletions shrink it). Callers maintain a running `ReplayState`
// and recompute `ArgumentPackageScope` at each step. To attribute a
// deletion correctly, query the helpers using the *prior-state* scope —
// after the transaction is applied, the deleted element id is no longer
// present in the package.
namespace core::audit {

// Element identity set owned by a single ArgumentPackage. We keep both `id`
// and `gid` because audit event payloads use whichever the source model
// produced.
struct ArgumentPackageScope {
    std::unordered_set<std::string> element_ids;
    std::unordered_set<std::string> element_gids;
};

// Collect every claim/reasoning/reference/asserted-relationship id and gid
// owned by `argument_package`.
ArgumentPackageScope CollectArgumentPackageScope(const sacm::ArgumentPackage& argument_package);

// Element identifiers referenced by a single AuditEvent. Recognized payload
// fields follow the conventions used by built-in commands:
//   - "generated_id"               (CreateTopGoal, CreateChildElement)
//   - "generated_relationship_id"  (CreateChildElement)
//   - "parent_id"                  (CreateChildElement)
//   - "element_id"                 (RemoveElement, future update commands)
//   - "deleted_ids"                (RemoveElement, array of strings)
//   - "target_id" / "source_id"    (future relationship commands)
// Unknown payload shapes contribute nothing.
std::vector<std::string> CollectEventElementIds(const AuditEvent& event);

// True if at least one event in `transaction` references an identifier that
// is a member of `scope` (matched against either `element_ids` or
// `element_gids`).
bool TransactionTouchesScope(const AuditTransaction& transaction, const ArgumentPackageScope& scope);

} // namespace core::audit
