#pragma once

// The wire contract between the MCP adapter and the running application.
//
// The adapter owns the MCP transport; Assurance Forge owns the model and every
// domain operation. This is the boundary between them, and it is deliberately
// ignorant of safety cases: it carries an operation name and two opaque JSON
// documents. Keeping domain vocabulary out of here is what stops the boundary
// from becoming a second, competing model of the argument.
//
// **Versioned on purpose.** The two binaries ship together but are not launched
// together: an MCP client configuration written months ago keeps pointing at
// whatever `assurance-forge-mcp` path it recorded, so a new application can
// easily meet an old adapter. Every frame carries `protocol`, and a mismatch is
// reported as one specific error the adapter can turn into an instruction, not
// as a parse failure that presents to the user as "the AI stopped working".
//
// Framing is newline-delimited JSON, matching `mcp/jsonrpc.h` so both sides of
// the adapter read the same way.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace bridge {

// Bump when a change would make an older peer misread a message. Adding an
// optional field to `args` or `result` is not such a change; renaming an
// operation, or altering what an existing field means, is.
inline constexpr int kProtocolVersion = 1;

// Stable string codes rather than numbers. These reach an AI client, and
// occasionally a user, through the adapter's error text -- `unauthorized` is
// self-describing in a log where `-32004` is not.
namespace error_code {
inline constexpr const char* kUnsupportedProtocol = "unsupported_protocol";
inline constexpr const char* kUnauthorized = "unauthorized";
inline constexpr const char* kNotInitialized = "not_initialized";
inline constexpr const char* kUnknownOperation = "unknown_operation";
inline constexpr const char* kBadRequest = "bad_request";
inline constexpr const char* kNoProject = "no_project";
inline constexpr const char* kNoCase = "no_case";
inline constexpr const char* kConsentWithheld = "consent_withheld";
inline constexpr const char* kInternal = "internal";
} // namespace error_code

// The handshake operation. Must be the first request on a connection; every
// other operation is refused with `not_initialized` until it succeeds.
inline constexpr const char* kHelloOperation = "hello";
// Updates the client label after MCP initialize supplies the real client name
// and version. Handled on the connection thread like hello, not by domain code.
inline constexpr const char* kIdentifyOperation = "identify";

struct Request {
    int protocol = kProtocolVersion;
    std::uint64_t id = 0;
    std::string op;
    // Proves the caller read the endpoint record, which lives in the user's own
    // runtime directory. Without it any local process could drive the
    // application; the transport's access control is the first gate and this is
    // the second.
    std::string token;
    nlohmann::json args = nlohmann::json::object();
};

struct Response {
    int protocol = kProtocolVersion;
    std::uint64_t id = 0;
    bool ok = false;
    nlohmann::json result = nlohmann::json::object();
    std::string error_code;
    std::string error_message;
};

std::string EncodeRequest(const Request& request);
std::string EncodeResponse(const Response& response);

// `error` describes why the text is not a message of that kind. A decode
// failure is never a protocol-version problem -- version checking happens on a
// well-formed message, so the two failures stay distinguishable.
bool DecodeRequest(const std::string& text, Request& out, std::string& error);
bool DecodeResponse(const std::string& text, Response& out, std::string& error);

Response MakeResult(std::uint64_t id, nlohmann::json result);
Response MakeError(std::uint64_t id, std::string code, std::string message);

// True when `protocol` is one this build can serve. Separated from decoding so
// a peer that speaks a future version still gets a courteous, versioned refusal
// rather than silence.
bool IsSupportedProtocol(int protocol);

// The refusal itself, worded for a human who has to fix it: it names both
// versions and what to do, because the person reading it is looking at an AI
// client that has stopped working and has no other clue.
std::string UnsupportedProtocolMessage(int peer_protocol);

} // namespace bridge
