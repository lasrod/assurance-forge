#pragma once

// JSON-RPC 2.0 plumbing for the MCP stdio transport.
//
// MCP's stdio transport frames each message as one line of JSON: messages are
// newline-delimited and must not contain embedded newlines. Every write goes
// through WriteMessage, which serializes compactly -- a pretty-printed object
// would span lines and desynchronize the stream for the rest of the session.
//
// Parsing and dispatch are deliberately separated from the streams so the whole
// protocol can be tested by feeding strings to Server::HandleMessage, without
// spawning a process.

#include <nlohmann/json.hpp>

#include <iosfwd>
#include <optional>
#include <string>

namespace mcp::jsonrpc {

// Standard JSON-RPC 2.0 error codes (specification section 5.1).
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;

// A well-formed request. A notification is a request carrying no `id` member;
// the specification forbids replying to one, which is why this is tracked
// explicitly rather than inferred from a null id (an explicit `"id": null` is a
// request, badly formed, and still gets an error response).
struct Request {
    std::string method;
    nlohmann::json params = nlohmann::json::object();
    nlohmann::json id = nullptr;
    bool is_notification = false;
};

// Exactly one of `request` / `error_response` is set.
struct ParseOutcome {
    std::optional<Request> request;
    std::optional<nlohmann::json> error_response;
};

ParseOutcome ParseRequest(const std::string& message);

nlohmann::json MakeResult(const nlohmann::json& id, nlohmann::json result);
nlohmann::json MakeError(const nlohmann::json& id, int code, const std::string& message);

// Reads one newline-delimited message. Returns false at end of stream. Blank
// lines are skipped rather than reported as parse errors, and a trailing CR is
// stripped so a client writing CRLF does not produce a trailing-garbage parse
// failure on every single message.
bool ReadMessage(std::istream& in, std::string& out_message);

// Writes one message followed by a newline, then flushes.
void WriteMessage(std::ostream& out, const nlohmann::json& message);

} // namespace mcp::jsonrpc
