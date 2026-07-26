#pragma once

// MCP request dispatch.
//
// `HandleMessage` takes a raw message and returns the response, so the entire
// protocol is exercisable from a unit test with no process, no pipes, and no
// timing. `Run` is the thin stdio loop around it.

#include "mcp/jsonrpc.h"
#include "mcp/session.h"

#include <nlohmann/json.hpp>

#include <iosfwd>
#include <optional>
#include <string>

namespace mcp {

// The MCP revision this server implements. `initialize` reports it and a client
// that cannot speak it is expected to close the connection. Pinning it -- rather
// than echoing whatever the client asks for -- means a protocol change surfaces
// as an explicit mismatch instead of as subtly wrong behaviour.
inline constexpr const char* kProtocolVersion = "2025-06-18";
inline constexpr const char* kServerName      = "assurance-forge";
inline constexpr const char* kServerVersion   = "0.1.0";

class Server {
  public:
    explicit Server(Session& session) : session_(session) {}

    // Returns nullopt when nothing should be written back, which is the case for
    // every notification -- JSON-RPC forbids responding to one.
    std::optional<nlohmann::json> HandleMessage(const std::string& message);

    void Run(std::istream& in, std::ostream& out);

  private:
    nlohmann::json Dispatch(const jsonrpc::Request& request);
    nlohmann::json HandleInitialize(const jsonrpc::Request& request);
    nlohmann::json HandleToolsList(const jsonrpc::Request& request);
    nlohmann::json HandleToolsCall(const jsonrpc::Request& request);

    Session& session_;
};

} // namespace mcp
