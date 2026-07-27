#include "mcp/server.h"

#include "mcp/tools.h"

#include <istream>
#include <ostream>

namespace mcp {
namespace {

// MCP wraps every tool result in content blocks. JSON payloads travel as pretty-
// printed text because that is what the model reads; the indentation is for the
// reader, not the protocol (the framing newline is added by WriteMessage, and
// the payload is a string, so this cannot break line framing).
nlohmann::json ContentResult(const ToolResult& result) {
    return nlohmann::json{
        {"content", nlohmann::json::array({nlohmann::json{{"type", "text"},
                                                          {"text", result.payload.dump(2)}}})},
        {"isError", result.is_error},
    };
}

std::string ClientLabelFrom(const nlohmann::json& params) {
    const nlohmann::json::const_iterator client = params.find("clientInfo");
    if (client == params.end() || !client->is_object()) {
        return "unknown client";
    }
    const nlohmann::json::const_iterator name = client->find("name");
    if (name == client->end() || !name->is_string()) {
        return "unknown client";
    }
    std::string label = name->get<std::string>();
    const nlohmann::json::const_iterator version = client->find("version");
    if (version != client->end() && version->is_string()) {
        label += " " + version->get<std::string>();
    }
    return label;
}

} // namespace

std::optional<nlohmann::json> Server::HandleMessage(const std::string& message) {
    jsonrpc::ParseOutcome parsed = jsonrpc::ParseRequest(message);
    if (parsed.error_response.has_value()) {
        return parsed.error_response;
    }

    const jsonrpc::Request& request = parsed.request.value();

    if (request.is_notification) {
        if (request.method == "notifications/initialized") {
            session_.mark_initialized();
        }
        // Unknown notifications are ignored rather than reported: the
        // specification forbids a response, so an error would have nowhere to go.
        return std::nullopt;
    }

    return Dispatch(request);
}

nlohmann::json Server::Dispatch(const jsonrpc::Request& request) {
    if (request.method == "initialize") {
        return HandleInitialize(request);
    }
    if (request.method == "ping") {
        return jsonrpc::MakeResult(request.id, nlohmann::json::object());
    }

    // Everything else requires a completed handshake. Enforcing it in one place
    // keeps each handler free of the check.
    if (!session_.initialized()) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidRequest,
                                  "Session is not initialized. Send \"initialize\" first.");
    }

    if (request.method == "tools/list") {
        return HandleToolsList(request);
    }
    if (request.method == "tools/call") {
        return HandleToolsCall(request);
    }
    return jsonrpc::MakeError(request.id, jsonrpc::kMethodNotFound,
                              "Unknown method: " + request.method);
}

nlohmann::json Server::HandleInitialize(const jsonrpc::Request& request) {
    if (request.params.is_object()) {
        session_.set_client_label(ClientLabelFrom(request.params));
    }
    // The handshake completes on `notifications/initialized`, but a client that
    // omits it should not be locked out of every subsequent call, so treat a
    // successful initialize as sufficient.
    session_.mark_initialized();

    return jsonrpc::MakeResult(
        request.id, nlohmann::json{
                        {"protocolVersion", kProtocolVersion},
                        {"capabilities", {{"tools", {{"listChanged", false}}}}},
                        {"serverInfo", {{"name", kServerName}, {"version", kServerVersion}}},
                    });
}

nlohmann::json Server::HandleToolsList(const jsonrpc::Request& request) {
    nlohmann::json tools = nlohmann::json::array();
    for (const ToolDefinition& tool : BuiltinTools()) {
        tools.push_back(nlohmann::json{{"name", tool.name},
                                       {"description", tool.description},
                                       {"inputSchema", tool.input_schema}});
    }
    return jsonrpc::MakeResult(request.id, nlohmann::json{{"tools", std::move(tools)}});
}

nlohmann::json Server::HandleToolsCall(const jsonrpc::Request& request) {
    if (!request.params.is_object()) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams,
                                  "\"params\" must be an object with a \"name\".");
    }

    const nlohmann::json::const_iterator name = request.params.find("name");
    if (name == request.params.end() || !name->is_string()) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams,
                                  "\"params.name\" is required and must be a string.");
    }

    const ToolDefinition* tool = FindTool(name->get<std::string>());
    if (tool == nullptr) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams,
                                  "Unknown tool: " + name->get<std::string>());
    }

    nlohmann::json                       arguments = nlohmann::json::object();
    const nlohmann::json::const_iterator supplied  = request.params.find("arguments");
    if (supplied != request.params.end() && supplied->is_object()) {
        arguments = *supplied;
    }

    // The consent gate. Reported as a tool error rather than a protocol error so
    // the model can relay the instruction to the user instead of the connection
    // faulting on something the user can fix.
    if (tool->returns_case_content && !session_.consent_granted()) {
        return jsonrpc::MakeResult(
            request.id,
            ContentResult(ToolResult::Error(
                "Assurance Forge has not been given permission to share this project over MCP. "
                "Turn on \"Allow AI clients to read and propose changes\" in Assurance Forge's "
                "Preferences, under MCP Server. It takes effect on the next call; there is no "
                "need to restart anything.")));
    }

    return jsonrpc::MakeResult(request.id, ContentResult(tool->handler(session_, arguments)));
}

void Server::Run(std::istream& in, std::ostream& out) {
    std::string message;
    while (jsonrpc::ReadMessage(in, message)) {
        const std::optional<nlohmann::json> response = HandleMessage(message);
        if (response.has_value()) {
            jsonrpc::WriteMessage(out, *response);
        }
    }
}

} // namespace mcp
