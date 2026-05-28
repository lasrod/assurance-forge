#pragma once

#include "core/audit/audit_diff.h"
#include "core/audit/audit_transaction.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// History highlight derivation (design §16.3 — UI integration).
//
// Given a target transaction sequence, produce a map from element id to the
// kind of change applied by that single transaction. The History Timeline
// uses this to tint reconstructed canvas nodes:
//   Added    — element was created by the transaction (green tint).
//   Modified — element was edited by the transaction (yellow tint).
//   Deleted  — element was removed by the transaction (red tint / ghost).
//
// This helper lives in core (no UI dependency) so the mapping can be unit
// tested in isolation. The UI layer translates HistoryHighlightKind to a
// concrete theme color at render time.
namespace core::audit {

enum class HistoryHighlightKind { Added, Modified, Deleted };

// Build per-element highlights for the transaction whose
// `transaction_sequence == target_sequence`. Returns an empty map when no
// such transaction exists (e.g. target_sequence == 0, meaning the initial
// snapshot — nothing has been applied yet).
std::unordered_map<std::string, HistoryHighlightKind>
BuildHistoryHighlightsForSequence(const std::vector<AuditTransaction>& transactions,
                                  std::uint64_t target_sequence);

// Build highlights directly from a change set. Exposed for callers that
// already have the change set in hand. Conflict precedence within a single
// transaction: Deleted > Added > Modified.
std::unordered_map<std::string, HistoryHighlightKind>
BuildHistoryHighlights(const AuditChangeSet& change_set);

} // namespace core::audit
