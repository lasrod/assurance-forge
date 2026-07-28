#include "bridge/protocol.h"

#include <utility>

namespace bridge {
namespace {

// A message is read from a pipe that any local process could in principle write
// to, so every field is checked rather than assumed. `value_or_default` keeps
// that from turning each accessor into four lines.
template <typename T>
T ValueOr(const nlohmann::json& object, const char* key, T fallback) {
    const nlohmann::json::const_iterator found = object.find(key);
    if (found == object.end()) {
        return fallback;
    }
    try {
        return found->get<T>();
    } catch (const nlohmann::json::exception&) {
        return fallback;
    }
}

nlohmann::json ObjectOrEmpty(const nlohmann::json& object, const char* key) {
    const nlohmann::json::const_iterator found = object.find(key);
    if (found == object.end() || !found->is_object()) {
        return nlohmann::json::object();
    }
    return *found;
}

bool ParseObject(const std::string& text, nlohmann::json& out, std::string& error) {
    out = nlohmann::json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        error = "Message is not valid JSON.";
        return false;
    }
    if (!out.is_object()) {
        error = "Message must be a JSON object.";
        return false;
    }
    return true;
}

} // namespace

std::string EncodeRequest(const Request& request) {
    return nlohmann::json{
        {"protocol", request.protocol}, {"id", request.id},     {"op", request.op},
        {"token", request.token},       {"args", request.args},
    }
        .dump();
}

std::string EncodeResponse(const Response& response) {
    nlohmann::json message{
        {"protocol", response.protocol},
        {"id", response.id},
        {"ok", response.ok},
    };
    if (response.ok) {
        message["result"] = response.result;
    } else {
        message["error"] = nlohmann::json{{"code", response.error_code},
                                          {"message", response.error_message}};
    }
    return message.dump();
}

bool DecodeRequest(const std::string& text, Request& out, std::string& error) {
    nlohmann::json message;
    if (!ParseObject(text, message, error)) {
        return false;
    }

    out.protocol = ValueOr<int>(message, "protocol", 0);
    out.id       = ValueOr<std::uint64_t>(message, "id", 0);
    out.op       = ValueOr<std::string>(message, "op", std::string());
    out.token    = ValueOr<std::string>(message, "token", std::string());
    out.args     = ObjectOrEmpty(message, "args");

    if (out.op.empty()) {
        error = "Request is missing \"op\".";
        return false;
    }
    return true;
}

bool DecodeResponse(const std::string& text, Response& out, std::string& error) {
    nlohmann::json message;
    if (!ParseObject(text, message, error)) {
        return false;
    }

    out.protocol = ValueOr<int>(message, "protocol", 0);
    out.id       = ValueOr<std::uint64_t>(message, "id", 0);
    out.ok       = ValueOr<bool>(message, "ok", false);
    out.result   = ObjectOrEmpty(message, "result");

    const nlohmann::json failure = ObjectOrEmpty(message, "error");
    out.error_code    = ValueOr<std::string>(failure, "code", std::string());
    out.error_message = ValueOr<std::string>(failure, "message", std::string());

    if (!out.ok && out.error_code.empty()) {
        // A failure with no code cannot be acted on, and silently treating it as
        // success would apply an empty result to a safety case.
        error = "Response reports failure without an error code.";
        return false;
    }
    return true;
}

Response MakeResult(std::uint64_t id, nlohmann::json result) {
    Response response;
    response.id     = id;
    response.ok     = true;
    response.result = std::move(result);
    return response;
}

Response MakeError(std::uint64_t id, std::string code, std::string message) {
    Response response;
    response.id            = id;
    response.ok            = false;
    response.error_code    = std::move(code);
    response.error_message = std::move(message);
    return response;
}

bool IsSupportedProtocol(int protocol) {
    return protocol == kProtocolVersion;
}

std::string UnsupportedProtocolMessage(int peer_protocol) {
    return "This Assurance Forge speaks bridge protocol " + std::to_string(kProtocolVersion) +
           ", and the connecting assurance-forge-mcp speaks " + std::to_string(peer_protocol) +
           ". They are from different builds. Point your AI client's configuration at the "
           "assurance-forge-mcp that shipped with this Assurance Forge -- Preferences has a "
           "\"Copy MCP client config\" button that emits the right path.";
}

} // namespace bridge
