#include "core/audit/audit_transaction.h"

#include <exception>

namespace core::audit {

using nlohmann::ordered_json;

ordered_json SerializeAuditTransaction(const AuditTransaction& tx) {
    ordered_json j;
    j["transaction_schema_version"] = tx.transaction_schema_version;
    j["transaction_sequence"] = tx.transaction_sequence;
    j["transaction_id"] = tx.transaction_id;
    j["timestamp"] = tx.timestamp;
    j["author"] = tx.author;
    j["command_name"] = tx.command_name;
    j["previous_transaction_hash"] = tx.previous_transaction_hash;
    ordered_json events = ordered_json::array();
    for (const AuditEvent& e : tx.events)
        events.push_back(SerializeAuditEvent(e));
    j["events"] = std::move(events);
    return j;
}

std::string SerializeAuditTransactionLine(const AuditTransaction& tx) {
    // Compact single-line form for JSONL. No trailing newline — callers add
    // it. We always use the same separators so that hashing is deterministic.
    return SerializeAuditTransaction(tx).dump();
}

bool ParseAuditTransaction(const ordered_json& j, AuditTransaction& out, std::string& error) {
    if (!j.is_object()) {
        error = "Transaction is not a JSON object";
        return false;
    }
    out = AuditTransaction{};
    try {
        if (auto it = j.find("transaction_schema_version"); it != j.end() && it->is_number_integer())
            out.transaction_schema_version = it->get<int>();
        if (auto it = j.find("transaction_sequence"); it != j.end() && it->is_number_unsigned())
            out.transaction_sequence = it->get<std::uint64_t>();
        if (auto it = j.find("transaction_id"); it != j.end() && it->is_string())
            out.transaction_id = it->get<std::string>();
        if (auto it = j.find("timestamp"); it != j.end() && it->is_string())
            out.timestamp = it->get<std::string>();
        if (auto it = j.find("author"); it != j.end() && it->is_string())
            out.author = it->get<std::string>();
        if (auto it = j.find("command_name"); it != j.end() && it->is_string())
            out.command_name = it->get<std::string>();
        if (auto it = j.find("previous_transaction_hash"); it != j.end() && it->is_string())
            out.previous_transaction_hash = it->get<std::string>();
        if (auto it = j.find("events"); it != j.end() && it->is_array()) {
            for (const auto& e : *it) {
                AuditEvent ev;
                std::string ev_err;
                if (!ParseAuditEvent(e, ev, ev_err)) {
                    error = ev_err;
                    return false;
                }
                out.events.push_back(std::move(ev));
            }
        }
    } catch (const std::exception& ex) {
        error = std::string("Failed to parse transaction: ") + ex.what();
        return false;
    }
    return true;
}

bool ParseAuditTransactionLine(const std::string& line, AuditTransaction& out, std::string& error) {
    ordered_json j;
    try {
        j = ordered_json::parse(line);
    } catch (const std::exception& ex) {
        error = std::string("Invalid transaction JSON: ") + ex.what();
        return false;
    }
    return ParseAuditTransaction(j, out, error);
}

} // namespace core::audit
