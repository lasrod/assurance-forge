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

struct AuditTransaction {
    int transaction_schema_version = kAuditTransactionSchemaVersion;
    std::uint64_t transaction_sequence = 0;
    std::string transaction_id;            // UUID-like opaque identifier
    std::string timestamp;                 // ISO-8601 UTC
    std::string author;                    // "system" or display name
    std::string command_name;              // Originating command, e.g. "CreateClaim"
    std::string previous_transaction_hash; // sha256 hex of previous tx line; empty for first
    std::vector<AuditEvent> events;
};

nlohmann::ordered_json SerializeAuditTransaction(const AuditTransaction& tx);
std::string SerializeAuditTransactionLine(const AuditTransaction& tx);
bool ParseAuditTransaction(const nlohmann::ordered_json& j, AuditTransaction& out, std::string& error);
bool ParseAuditTransactionLine(const std::string& line, AuditTransaction& out, std::string& error);

} // namespace core::audit
