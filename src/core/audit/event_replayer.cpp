#include "core/audit/event_replayer.h"

#include "core/commands/element_commands.h"
#include "core/element_factory.h"

#include <sstream>

namespace core::audit {

namespace {

std::string FormatLocation(std::uint64_t tx_seq, std::uint64_t ev_seq, const std::string& event_type) {
    std::ostringstream oss;
    oss << "transaction " << tx_seq << " event " << ev_seq << " (" << event_type << ")";
    return oss.str();
}

bool ApplyEvent(ReplayState& state,
                std::uint64_t tx_seq,
                const AuditEvent& event,
                std::string& out_error) {
    const std::string& type = event.event_type;
    const auto& payload = event.payload;

    auto require_string = [&](const char* key, std::string& dest) -> bool {
        auto it = payload.find(key);
        if (it == payload.end() || !it->is_string()) {
            out_error = "Missing or non-string payload field '" + std::string(key) + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        dest = it->get<std::string>();
        return true;
    };

    if (type == "CreateTopGoal") {
        std::string element_id;
        if (!require_string("generated_id", element_id))
            return false;
        std::string err;
        if (!core::AddTopGoalWithId(state.model, &state.package, element_id, err)) {
            out_error = "AddTopGoalWithId failed at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + err;
            return false;
        }
        return true;
    }

    if (type == "CreateChildElement") {
        std::string parent_id, kind_token, element_id, relationship_id;
        if (!require_string("parent_id", parent_id))
            return false;
        if (!require_string("kind", kind_token))
            return false;
        if (!require_string("generated_id", element_id))
            return false;
        if (!require_string("generated_relationship_id", relationship_id))
            return false;
        core::NewElementKind kind;
        if (!commands::NewElementKindFromToken(kind_token, kind)) {
            out_error = "Unknown kind token '" + kind_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        std::string err;
        if (!core::AddChildElementWithIds(state.model, &state.package, parent_id, kind, element_id,
                                          relationship_id, err)) {
            out_error = "AddChildElementWithIds failed at " +
                        FormatLocation(tx_seq, event.event_sequence, type) + ": " + err;
            return false;
        }
        return true;
    }

    if (type == "RemoveElement") {
        std::string element_id, mode_token;
        if (!require_string("element_id", element_id))
            return false;
        if (!require_string("mode", mode_token))
            return false;
        core::RemoveMode mode;
        if (!commands::RemoveModeFromToken(mode_token, mode)) {
            out_error = "Unknown remove mode token '" + mode_token + "' at " +
                        FormatLocation(tx_seq, event.event_sequence, type);
            return false;
        }
        std::string err;
        if (!core::RemoveElement(state.model, &state.package, element_id, mode, err)) {
            out_error = "RemoveElement failed at " + FormatLocation(tx_seq, event.event_sequence, type) +
                        ": " + err;
            return false;
        }
        return true;
    }

    out_error = "Unknown event type '" + type + "' at " +
                FormatLocation(tx_seq, event.event_sequence, type);
    return false;
}

} // namespace

std::expected<ReplayState, std::string> Replayer::ReplayFrom(
    const parser::AssuranceCase&         snapshot_model,
    const sacm::AssuranceCasePackage&    snapshot_package,
    const std::vector<AuditTransaction>& transactions,
    std::uint64_t                        up_to_transaction_sequence) {

    ReplayState state{snapshot_model, snapshot_package};

    for (const AuditTransaction& tx : transactions) {
        if (tx.transaction_sequence > up_to_transaction_sequence)
            continue;
        for (const AuditEvent& event : tx.events) {
            std::string err;
            if (!ApplyEvent(state, tx.transaction_sequence, event, err))
                return std::unexpected(std::move(err));
        }
    }

    return state;
}

} // namespace core::audit
