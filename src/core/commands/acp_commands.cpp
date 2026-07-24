#include "core/commands/acp_commands.h"

#include "core/acp/acp_editing.h"
#include "core/commands/library_bridge.h"

namespace core::commands {

nlohmann::ordered_json SerializeAcpRecord(const parser::AcpRecord& acp) {
    nlohmann::ordered_json json = nlohmann::ordered_json::object();
    json["id"]                  = acp.id;
    json["name"]                = acp.name;
    json["target_kind"]         = acp.target_kind;
    json["target_id"]           = acp.target_id;
    json["resolution_kind"]     = acp.resolution_kind;
    json["text"]                = acp.text;
    json["confidence_claim_id"] = acp.confidence_claim_id;
    json["argument_package_id"] = acp.argument_package_id;
    json["top_goal_id"]         = acp.top_goal_id;
    return json;
}

bool DeserializeAcpRecord(const nlohmann::ordered_json& json, parser::AcpRecord& out, std::string& error) {
    if (!json.is_object()) {
        error = "ACP record payload is not a JSON object.";
        return false;
    }
    const auto read = [&](const char* key, std::string& dest) -> bool {
        auto it = json.find(key);
        if (it == json.end() || !it->is_string()) {
            error = "Missing or non-string ACP record field '" + std::string(key) + "'.";
            return false;
        }
        dest = it->get<std::string>();
        return true;
    };
    parser::AcpRecord record;
    if (!read("id", record.id) || !read("name", record.name) || !read("target_kind", record.target_kind) ||
        !read("target_id", record.target_id) || !read("resolution_kind", record.resolution_kind) ||
        !read("text", record.text) || !read("confidence_claim_id", record.confidence_claim_id) ||
        !read("argument_package_id", record.argument_package_id) || !read("top_goal_id", record.top_goal_id)) {
        return false;
    }
    out = std::move(record);
    return true;
}

bool AddAcpCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    core::acp::AcpEditResult   result;
    const LibraryBridgeMutator mutate = [&](parser::AssuranceCase&         model,
                                            sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        result = core::acp::AddAcp(model, &package, target_kind_, target_id_);
        if (!result.error.empty()) {
            err = result.error;
            return false;
        }
        if (!result.changed)
            return false;
        return true;
    };
    if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;
    generated_acp_id_ = result.acp_id;

    out_event.event_type = "AddAcp";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["target_kind"]      = target_kind_;
    out_event.payload["target_id"]        = target_id_;
    out_event.payload["generated_acp_id"] = generated_acp_id_;
    return true;
}

bool RemoveAcpCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    const LibraryBridgeMutator mutate = [&](parser::AssuranceCase&         model,
                                            sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        core::acp::AcpEditResult result = core::acp::RemoveAcp(model, &package, acp_id_);
        if (!result.error.empty()) {
            err = result.error;
            return false;
        }
        // A benign no-op (nothing removed, no error) records no transaction: the
        // mutator returns false with `err` left empty, so the bus appends nothing
        // and the caller treats an empty-error failure as "nothing happened",
        // mirroring the controller's `if (!result.changed) return false`.
        if (!result.changed)
            return false;
        return true;
    };
    if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;

    out_event.event_type = "RemoveAcp";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["acp_id"] = acp_id_;
    return true;
}

bool UpsertAcpCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event, std::string& out_error) {
    const LibraryBridgeMutator mutate = [&](parser::AssuranceCase&         model,
                                            sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        core::acp::AcpEditResult result = core::acp::UpsertAcp(model, &package, acp_);
        if (!result.error.empty()) {
            err = result.error;
            return false;
        }
        if (!result.changed)
            return false;
        return true;
    };
    if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;

    out_event.event_type = "UpsertAcp";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["acp"] = SerializeAcpRecord(acp_);
    return true;
}

bool CreateConfidenceArgumentTreeForAcpCommand::Apply(CommandContext& ctx, audit::AuditEvent& out_event,
                                                      std::string& out_error) {
    core::acp::AcpEditResult   result;
    const LibraryBridgeMutator mutate = [&](parser::AssuranceCase&         model,
                                            sacm::AssuranceCasePackage& package, std::string& err) -> bool {
        result = core::acp::CreateConfidenceArgumentTreeForAcp(model, &package, acp_id_);
        if (!result.error.empty()) {
            err = result.error;
            return false;
        }
        if (!result.changed)
            return false;
        return true;
    };
    if (!ApplyLibraryPrimaryOrLegacy(ctx, mutate, out_error))
        return false;
    argument_package_id_ = result.argument_package_id;
    top_goal_id_         = result.top_goal_id;

    out_event.event_type = "CreateConfidenceArgumentTree";
    out_event.payload    = nlohmann::ordered_json::object();
    out_event.payload["acp_id"]              = acp_id_;
    out_event.payload["argument_package_id"] = argument_package_id_;
    out_event.payload["top_goal_id"]         = top_goal_id_;
    return true;
}

} // namespace core::commands
