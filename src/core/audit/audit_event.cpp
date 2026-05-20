#include "core/audit/audit_event.h"

#include <exception>

namespace core::audit {

using nlohmann::ordered_json;

ordered_json SerializeAuditEvent(const AuditEvent& event) {
    ordered_json j;
    j["event_sequence"] = event.event_sequence;
    j["event_type"] = event.event_type;
    j["payload"] = event.payload;
    j["patch"] = event.patch;
    return j;
}

bool ParseAuditEvent(const ordered_json& j, AuditEvent& out, std::string& error) {
    if (!j.is_object()) {
        error = "Audit event is not a JSON object";
        return false;
    }
    out = AuditEvent{};
    try {
        if (auto it = j.find("event_sequence"); it != j.end() && it->is_number_unsigned())
            out.event_sequence = it->get<std::uint64_t>();
        if (auto it = j.find("event_type"); it != j.end() && it->is_string())
            out.event_type = it->get<std::string>();
        if (auto it = j.find("payload"); it != j.end())
            out.payload = *it;
        if (auto it = j.find("patch"); it != j.end())
            out.patch = *it;
    } catch (const std::exception& ex) {
        error = std::string("Failed to parse audit event: ") + ex.what();
        return false;
    }
    return true;
}

} // namespace core::audit
