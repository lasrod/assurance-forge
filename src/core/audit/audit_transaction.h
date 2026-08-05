#pragma once

#include "core/audit/audit_event.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// A transaction groups one or more audit events that were applied atomically
// by a single command (design §9). Transactions form a hash-chained log:
// every transaction records the sha256 of the previous transaction's
// serialized line, so any tampering with the on-disk log can be detected.
namespace core::audit {

inline constexpr int kAuditTransactionSchemaVersion = 1;

// Where a promoted draft change came from, beyond the name of the person who
// approved it.
//
// `author` already records the approver and the contributing source labels, but
// it is one display string. This is the machine-readable trace: which groups
// were promoted, which guidelines they served, which review items they answered,
// and the reasoning each author gave. The question "an AI proposed a change to
// this safety argument -- which one, why, and against which guideline?" has to
// be answerable from the log a year later, not from a draft that promotion
// consumed.
//
// **Written only for a draft promotion**, and serialized only when non-empty, so
// every other transaction's bytes are unchanged. The schema version stays at 1
// deliberately: an older reader ignores the key and loses the attribution, which
// is strictly better than bumping the version and having it reject the whole log.
struct DraftPromotionRecord {
    // In promotion order, which is materialization order.
    std::vector<std::string> group_ids;
    // Deduplicated contributing sources -- "Claude Code", "SCCG AI Review".
    std::vector<std::string> source_labels;
    std::vector<std::string> guideline_ids;
    std::vector<std::string> review_item_ids;
    // One entry per group that gave a reason, as `"<group title>: <rationale>"`.
    // Kept as prose rather than keyed by group id: it is written to be read by a
    // person auditing the argument, and a group id means nothing to them once
    // the draft is gone.
    std::vector<std::string> rationales;

    bool empty() const {
        return group_ids.empty() && source_labels.empty() && guideline_ids.empty() && review_item_ids.empty() &&
               rationales.empty();
    }
};

struct AuditTransaction {
    int transaction_schema_version = kAuditTransactionSchemaVersion;
    std::uint64_t transaction_sequence = 0;
    std::string transaction_id;            // UUID-like opaque identifier
    std::string timestamp;                 // ISO-8601 UTC
    std::string author;                    // "system" or display name
    std::string command_name;              // Originating command, e.g. "CreateClaim"
    std::string previous_transaction_hash; // sha256 hex of previous tx line; empty for first
    std::vector<AuditEvent> events;
    DraftPromotionRecord draft_promotion;
};

nlohmann::ordered_json SerializeAuditTransaction(const AuditTransaction& tx);
std::string SerializeAuditTransactionLine(const AuditTransaction& tx);
bool ParseAuditTransaction(const nlohmann::ordered_json& j, AuditTransaction& out, std::string& error);
bool ParseAuditTransactionLine(const std::string& line, AuditTransaction& out, std::string& error);

} // namespace core::audit
