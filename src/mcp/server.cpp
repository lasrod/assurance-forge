#include "mcp/server.h"

#include "mcp/guidance.h"
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
        {"content", nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", result.payload.dump(2)}}})},
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
    // Keyed on the request rather than on the error, because the request is what
    // gets dereferenced below. `ParseRequest` fills exactly one of the two --
    // every early return produces an error response, and the one path that
    // produces a request produces no error -- but that invariant lives in the
    // function rather than in the type, and reading it off the wrong field left
    // this line one refactor away from throwing `bad_optional_access` inside a
    // server whose whole job is to survive whatever a client sends.
    if (!parsed.request.has_value()) {
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
        return jsonrpc::MakeError(
            request.id, jsonrpc::kInvalidRequest, "Session is not initialized. Send \"initialize\" first.");
    }

    if (request.method == "tools/list") {
        return HandleToolsList(request);
    }
    if (request.method == "resources/list") {
        return HandleResourcesList(request);
    }
    if (request.method == "resources/templates/list") {
        return HandleResourcesTemplatesList(request);
    }
    if (request.method == "resources/read") {
        return HandleResourcesRead(request);
    }
    if (request.method == "prompts/list") {
        return HandlePromptsList(request);
    }
    if (request.method == "prompts/get") {
        return HandlePromptsGet(request);
    }
    if (request.method == "tools/call") {
        return HandleToolsCall(request);
    }
    return jsonrpc::MakeError(request.id, jsonrpc::kMethodNotFound, "Unknown method: " + request.method);
}

nlohmann::json Server::HandleInitialize(const jsonrpc::Request& request) {
    if (request.params.is_object()) {
        session_.set_client_label(ClientLabelFrom(request.params));
    }
    // The handshake completes on `notifications/initialized`, but a client that
    // omits it should not be locked out of every subsequent call, so treat a
    // successful initialize as sufficient.
    session_.mark_initialized();

    // The mode is re-evaluated per call, so this is a statement of *now*, not a
    // promise -- but a client that never calls get_connection_status should
    // still learn from the handshake what kind of session it got and that a
    // missing application heals without restarting anything.
    //
    // The authoring doctrine rides along because this field is the one text a
    // client may hand its model without anyone asking -- the user's prompt
    // rarely says what SCCG says, so the handshake has to. Clients differ in
    // whether they surface instructions at all, which is why the same doctrine
    // also travels on the pre-write read results (see mcp/tools.cpp).
    const std::string mode_statement =
        session_.connected()
            ? "Connected to a running Assurance Forge" +
                  (session_.application_version().empty() ? std::string() : " " + session_.application_version()) +
                  ". Reads answer from the live integrated working draft; staged changes appear on the user's "
                  "canvas and are promoted only by the user, in the application. If the connection is ever "
                  "lost, this session reconnects automatically on the next call."
            : "Assurance Forge is not running with this project open, so this session serves read-only from "
              "the last accepted version on disk and draft tools are unavailable. The session connects "
              "automatically once the application has the project open -- call get_connection_status to "
              "check, and simply retry after the user starts Assurance Forge.";
    const std::string instructions = mode_statement + "\n\n" + AuthoringDoctrine();

    return jsonrpc::MakeResult(request.id,
                               nlohmann::json{
                                   {"protocolVersion", kProtocolVersion},
                                   {"capabilities",
                                    {{"tools", {{"listChanged", false}}},
                                     // SCCG travels as resources and prompts so an agent has
                                     // the house rules before it writes, rather than being
                                     // corrected afterwards. See mcp/guidance.h.
                                     {"resources", {{"subscribe", false}, {"listChanged", false}}},
                                     {"prompts", {{"listChanged", false}}}}},
                                   {"serverInfo", {{"name", kServerName}, {"version", kServerVersion}}},
                                   {"instructions", instructions},
                               });
}

nlohmann::json Server::HandleToolsList(const jsonrpc::Request& request) {
    nlohmann::json tools = nlohmann::json::array();
    for (const ToolDefinition& tool : BuiltinTools()) {
        tools.push_back(
            nlohmann::json{{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.input_schema}});
    }
    return jsonrpc::MakeResult(request.id, nlohmann::json{{"tools", std::move(tools)}});
}

nlohmann::json Server::HandleResourcesList(const jsonrpc::Request& request) {
    nlohmann::json resources = nlohmann::json::array();
    for (const ResourceDefinition& resource : BuiltinResources()) {
        resources.push_back(nlohmann::json{{"uri", resource.uri},
                                           {"name", resource.name},
                                           {"description", resource.description},
                                           {"mimeType", resource.mime_type}});
    }
    return jsonrpc::MakeResult(request.id, nlohmann::json{{"resources", std::move(resources)}});
}

nlohmann::json Server::HandleResourcesTemplatesList(const jsonrpc::Request& request) {
    nlohmann::json templates = nlohmann::json::array();
    for (const ResourceTemplateDefinition& definition : BuiltinResourceTemplates()) {
        templates.push_back(nlohmann::json{{"uriTemplate", definition.uri_template},
                                           {"name", definition.name},
                                           {"description", definition.description},
                                           {"mimeType", definition.mime_type}});
    }
    return jsonrpc::MakeResult(request.id, nlohmann::json{{"resourceTemplates", std::move(templates)}});
}

nlohmann::json Server::HandleResourcesRead(const jsonrpc::Request& request) {
    if (!request.params.is_object()) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams, "\"params\" must be an object with a \"uri\".");
    }
    const nlohmann::json::const_iterator uri = request.params.find("uri");
    if (uri == request.params.end() || !uri->is_string()) {
        return jsonrpc::MakeError(
            request.id, jsonrpc::kInvalidParams, "\"params.uri\" is required and must be a string.");
    }

    bool found = false;
    std::string error;
    const std::string text = ReadResource(uri->get<std::string>(), found, error);
    if (!found) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams, "Unknown resource: " + uri->get<std::string>());
    }
    if (!error.empty()) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInternalError, error);
    }

    return jsonrpc::MakeResult(
        request.id,
        nlohmann::json{{"contents",
                        nlohmann::json::array({nlohmann::json{
                            {"uri", uri->get<std::string>()}, {"mimeType", "text/markdown"}, {"text", text}}})}});
}

nlohmann::json Server::HandlePromptsList(const jsonrpc::Request& request) {
    nlohmann::json prompts = nlohmann::json::array();
    for (const PromptDefinition& prompt : BuiltinPrompts()) {
        nlohmann::json arguments = nlohmann::json::array();
        for (const PromptArgument& argument : prompt.arguments) {
            arguments.push_back(nlohmann::json{
                {"name", argument.name}, {"description", argument.description}, {"required", argument.required}});
        }
        prompts.push_back(nlohmann::json{
            {"name", prompt.name}, {"description", prompt.description}, {"arguments", std::move(arguments)}});
    }
    return jsonrpc::MakeResult(request.id, nlohmann::json{{"prompts", std::move(prompts)}});
}

nlohmann::json Server::HandlePromptsGet(const jsonrpc::Request& request) {
    if (!request.params.is_object()) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams, "\"params\" must be an object with a \"name\".");
    }
    const nlohmann::json::const_iterator name = request.params.find("name");
    if (name == request.params.end() || !name->is_string()) {
        return jsonrpc::MakeError(
            request.id, jsonrpc::kInvalidParams, "\"params.name\" is required and must be a string.");
    }

    nlohmann::json arguments = nlohmann::json::object();
    const nlohmann::json::const_iterator supplied = request.params.find("arguments");
    if (supplied != request.params.end() && supplied->is_object()) {
        arguments = *supplied;
    }

    const std::string text = BuildPrompt(name->get<std::string>(), arguments);
    if (text.empty()) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams, "Unknown prompt: " + name->get<std::string>());
    }

    return jsonrpc::MakeResult(
        request.id,
        nlohmann::json{{"messages",
                        nlohmann::json::array(
                            {nlohmann::json{{"role", "user"}, {"content", {{"type", "text"}, {"text", text}}}}})}});
}

nlohmann::json Server::HandleToolsCall(const jsonrpc::Request& request) {
    if (!request.params.is_object()) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams, "\"params\" must be an object with a \"name\".");
    }

    const nlohmann::json::const_iterator name = request.params.find("name");
    if (name == request.params.end() || !name->is_string()) {
        return jsonrpc::MakeError(
            request.id, jsonrpc::kInvalidParams, "\"params.name\" is required and must be a string.");
    }

    const ToolDefinition* tool = FindTool(name->get<std::string>());
    if (tool == nullptr) {
        return jsonrpc::MakeError(request.id, jsonrpc::kInvalidParams, "Unknown tool: " + name->get<std::string>());
    }

    nlohmann::json arguments = nlohmann::json::object();
    const nlohmann::json::const_iterator supplied = request.params.find("arguments");
    if (supplied != request.params.end() && supplied->is_object()) {
        arguments = *supplied;
    }

    // The consent gate. Reported as a tool error rather than a protocol error so
    // the model can relay the instruction to the user instead of the connection
    // faulting on something the user can fix.
    if (tool->returns_case_content && !session_.consent_granted()) {
        return jsonrpc::MakeResult(request.id,
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
