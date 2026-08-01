#include "core/audit/history_highlights.h"

namespace core::audit {

std::unordered_map<std::string, HistoryHighlightKind> BuildHistoryHighlights(const AuditChangeSet& cs) {
    std::unordered_map<std::string, HistoryHighlightKind> out;
    // Insert in low-precedence → high-precedence order so later inserts win.
    for (const std::string& id : cs.modified)
        out[id] = HistoryHighlightKind::Modified;
    for (const std::string& id : cs.added)
        out[id] = HistoryHighlightKind::Added;
    for (const std::string& id : cs.deleted)
        out[id] = HistoryHighlightKind::Deleted;
    return out;
}

std::unordered_map<std::string, HistoryHighlightKind>
BuildHistoryHighlightsForSequence(const std::vector<AuditTransaction>& transactions, std::uint64_t target_sequence) {
    if (target_sequence == 0)
        return {};
    for (const AuditTransaction& tx : transactions) {
        if (tx.transaction_sequence == target_sequence)
            return BuildHistoryHighlights(ComputeChangeSet(tx));
    }
    return {};
}

} // namespace core::audit
