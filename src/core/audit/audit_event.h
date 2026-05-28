#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

// A single semantic event recorded by the audit subsystem (design §8).
//
// Phase 1 only persists a flexible {event_type, payload, patch} triple;
// strongly-typed event variants (CreateClaim, MoveNode, …) are introduced in
// Phase 2 and serialize into these same fields. Unknown event_type strings
// are preserved on round-trip so logs written by newer versions of the
// application remain readable.
namespace core::audit {

struct AuditEvent {
    // Monotonic per-event identifier, assigned by the event store at append
    // time. Stable across reloads.
    std::uint64_t event_sequence = 0;
    // Free-form discriminator string. Convention: PascalCase verb-noun
    // ("CreateClaim", "MoveNode"). Empty string is reserved.
    std::string event_type;
    // Forward payload. Whatever the typed event handler needs to apply the
    // event when replaying from a snapshot.
    nlohmann::ordered_json payload;
    // Reverse / undo patch. Populated by the command bus when an event is
    // generated from a user action; empty for synthetic events.
    nlohmann::ordered_json patch;
};

nlohmann::ordered_json SerializeAuditEvent(const AuditEvent& event);
bool ParseAuditEvent(const nlohmann::ordered_json& j, AuditEvent& out, std::string& error);

} // namespace core::audit
