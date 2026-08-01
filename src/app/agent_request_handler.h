#pragma once

// Turns one bridge request into one bridge response, against the live model.
//
// Runs on the frame thread, called from `AppRuntime::PollAgentBridge`. It is a
// free function over the pieces it needs rather than a method on `AppRuntime`
// so it can be tested without standing up a window, an event loop, or a
// controller graph -- the same reason the adoption decision in the code this
// replaces was separated from its I/O.
//
// Read operations are executed by `agent/`, the same implementations
// `assurance-forge-mcp` uses when no application is running. Sharing them is
// what stops an agent getting different answers depending on whether a window
// happens to be open.

#include "bridge/protocol.h"
#include "core/app_state.h"
#include "core/changesets/change_set_store.h"

#include <cstdint>
#include <functional>
#include <string>

namespace app {

// Reported to a connecting client in the handshake so a mismatch between the
// application and the adapter can be described in terms a user recognises.
// Tracks `mcp::kServerVersion`: the two binaries ship together.
inline constexpr const char* kAppVersion = "0.1.0";

// What the handler is allowed to touch. Reads only, for now: writes arrive with
// change sets and need the command bus.
struct AgentRequestContext {
    const core::AppState& state;
    // How this project was addressed, echoed back so a client can confirm it is
    // talking to the project it meant.
    std::string project_path;
    // The connected client, for attribution on anything a request produces.
    std::string client_label;
    // Switches the application to another of the project's argument files.
    // Supplied by the caller rather than performed here because opening a file
    // touches controllers, the command bus and the audit store -- all of which
    // belong to the runtime, not to a request handler. Empty when the caller
    // cannot honour it.
    std::function<bool(const std::string& relative_path, std::string& error)> open_case_file;

    // The change sets being built against this project. Null when there is no
    // project, in which case every change operation is refused.
    core::changesets::ChangeSetStore* change_sets = nullptr;
    // Which connection is asking, so an agent's operations reach its own change
    // set rather than another client's.
    std::uint64_t connection_id = 0;
};

bridge::Response HandleAgentRequest(const bridge::Request& request, const AgentRequestContext& context);

} // namespace app
