#include "mcp/jsonrpc.h"

#include <istream>
#include <ostream>

namespace mcp::jsonrpc {
namespace {

// A JSON-RPC id must be a string, a number, or null. Anything else is a
// malformed request, and echoing it back into the error response would be
// propagating the malformation rather than reporting it.
bool IsUsableId(const nlohmann::json& id) {
    return id.is_string() || id.is_number() || id.is_null();
}

} // namespace

ParseOutcome ParseRequest(const std::string& message) {
    ParseOutcome outcome;

    nlohmann::json parsed = nlohmann::json::parse(message, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        outcome.error_response = MakeError(nullptr, kParseError, "Message is not valid JSON");
        return outcome;
    }
    if (!parsed.is_object()) {
        outcome.error_response = MakeError(nullptr, kInvalidRequest, "Request must be a JSON object");
        return outcome;
    }

    // Recover the id before validating the rest, so a request that is otherwise
    // malformed still gets an error the client can correlate to its call.
    nlohmann::json id = nullptr;
    const bool has_id = parsed.contains("id");
    if (has_id && IsUsableId(parsed["id"])) {
        id = parsed["id"];
    }

    const nlohmann::json::const_iterator method = parsed.find("method");
    if (method == parsed.end() || !method->is_string()) {
        outcome.error_response = MakeError(id, kInvalidRequest, "Request is missing a string \"method\"");
        return outcome;
    }

    Request request;
    request.method = method->get<std::string>();
    request.id = id;
    request.is_notification = !has_id;

    const nlohmann::json::const_iterator params = parsed.find("params");
    if (params != parsed.end() && !params->is_null()) {
        if (!params->is_object() && !params->is_array()) {
            outcome.error_response = MakeError(id, kInvalidParams, "\"params\" must be an object or an array");
            return outcome;
        }
        request.params = *params;
    }

    outcome.request = std::move(request);
    return outcome;
}

nlohmann::json MakeResult(const nlohmann::json& id, nlohmann::json result) {
    return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

nlohmann::json MakeError(const nlohmann::json& id, int code, const std::string& message) {
    return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

bool ReadMessage(std::istream& in, std::string& out_message) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        out_message = std::move(line);
        return true;
    }
    return false;
}

void WriteMessage(std::ostream& out, const nlohmann::json& message) {
    out << message.dump() << '\n';
    out.flush();
}

} // namespace mcp::jsonrpc
