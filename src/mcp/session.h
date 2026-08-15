#pragma once

// One MCP connection's view of an Assurance Forge project.
//
// **Connected.** Assurance Forge has this project open. The session holds no
// model of its own; every operation goes over the bridge and is answered by the
// application from the integrated working argument the user is looking at. This
// is the mode that matters: the agent and the user see one thing, and draft
// changes are serialized through the application's revisioned workspace.
//
// **Offline.** No application is reachable, so the session loads the project
// itself and answers reads from accepted SACM. It is **read-only**. A headless
// process cannot own the user's integrated draft or present unaccepted changes,
// and a second project writer is exactly the fault this design removes.
//
// **Dynamic** (no configured project, ADR 0014). The session initializes with
// nothing -- no application, no project -- and discovers the running instance
// at call time. It connects *unbound*: the application serves it no project
// content, and every project operation is refused with
// `project_access_required` until an access grant binds the session to the
// open project. The grant flow is the next phase; until it exists a dynamic
// session can report status and nothing else, which is the fail-closed
// reading of ADR 0014's second gate.
//
// **Explicit offline** (`--offline-project`). The offline mode above, chosen
// deliberately: reads accepted SACM from the named path, never connects, and
// never promotes itself to a running application.
//
// The mode is re-evaluated on every call. MCP clients launch this process at
// *client* startup, so "the application is not running yet" and "the
// application restarted" are ordinary situations, not faults: before each
// operation the session tries to reach the application, an offline session
// promotes itself when the application appears, and a lost connection is
// retried on the next call. An earlier design decided the mode once, at open,
// on the argument that quietly changing capability mid-conversation is worse
// than asking the user to reconnect -- experience showed the permanent dead
// session was the worse trap, so the change is announced instead of quiet:
// every transition is visible in the response envelope (`view`, revisions),
// the connection errors say the session will reconnect by itself, and
// `get_connection_status` reports the current mode on demand.
//
// The session id is generated once at open and survives reconnects, so draft
// groups this session authored remain its own across an application restart.

#include "core/app_state.h"

#include "bridge/protocol.h"
#include "bridge/transport.h"

#include <filesystem>
#include <memory>
#include <string>

namespace mcp {

class Session {
public:
    enum class Mode {
        // Answering from a running Assurance Forge over the bridge.
        Connected,
        // No application reachable right now. Reads come from a copy this
        // process loaded, when it loaded one; every call retries the bridge.
        Offline,
    };

    struct Config {
        // A project directory, a project manifest, or a bare SACM file. Empty
        // means a dynamic session: discover the running application at call
        // time and connect unbound.
        std::filesystem::path project_path;
        // Empty resolves to core::UserSettingsFilePath(). Tests point this at a
        // temporary file so a developer's real consent setting cannot make a
        // test pass that would fail on a clean machine.
        std::filesystem::path settings_path;
        // Tests set this to keep a session offline even when the developer
        // happens to have the project open in Assurance Forge.
        bool never_connect = false;
        // --offline-project: read the named path's accepted SACM, read-only,
        // and never look for a running application. Requires project_path.
        bool offline_only = false;
    };

    static std::unique_ptr<Session> Open(Config config, std::string& error);
    ~Session();

    Mode mode() const {
        return mode_;
    }
    bool connected() const {
        return mode_ == Mode::Connected;
    }

    // The application's version, when connected. Empty offline.
    const std::string& application_version() const {
        return application_version_;
    }

    // Runs one operation. Connected, this is a bridge round trip; offline it is
    // executed against the loaded copy. Callers do not branch on the mode --
    // that is the point of routing everything through here.
    struct OperationResult {
        nlohmann::json payload = nlohmann::json::object();
        bool is_error = false;
        // True when the operation failed because no application is reachable,
        // so a caller can say so rather than reporting a generic failure.
        bool needs_application = false;
        // True when a bridge round trip failed mid-call. Internal to Run's
        // reconnect handling; by the time a result leaves Run the flag has been
        // folded into the error text.
        bool connection_lost = false;
    };
    OperationResult Run(const std::string& op, const nlohmann::json& args);

    // True when the user has enabled the MCP server in settings.json. Every tool
    // that returns assurance-case content is refused until this is true.
    //
    // Read from disk on every call rather than cached when the session opens.
    // Consent is revocable, and a cached grant would keep serving a safety case
    // for as long as the client happened to leave the process running -- the
    // user turns the switch off in Preferences and nothing stops. Re-reading
    // costs one small file read per tool call, which is nothing at
    // human-conversation rates, and makes ADR 0007's promise that revocation
    // takes effect on the next call actually true.
    bool consent_granted() const;

    // MCP requires `initialize` before any other request. Tracking it here keeps
    // the ordering rule enforceable in one place.
    bool initialized() const {
        return initialized_;
    }
    void mark_initialized() {
        initialized_ = true;
    }

    // Client name and version from the initialize handshake, used to attribute
    // anything this session produces.
    void set_client_label(std::string label);
    const std::string& client_label() const {
        return client_label_;
    }
    const std::string& session_id() const {
        return session_id_;
    }

    const std::filesystem::path& project_path() const {
        return project_path_;
    }

private:
    Session() = default;

    bool ConnectToApplication(std::string& error);
    bool OpenOffline(std::string& error);
    // Re-establishes the bridge connection when there is none. Cheap when
    // already connected, and cheap when the application is absent -- one
    // endpoint-record read that usually finds no file. Never called when the
    // session was opened with `never_connect`.
    bool EnsureConnected();
    OperationResult DescribeConnection();
    OperationResult RunOverBridge(const std::string& op, const nlohmann::json& args);
    OperationResult RunOffline(const std::string& op, const nlohmann::json& args);

    Mode mode_ = Mode::Offline;
    core::AppState state_;
    std::unique_ptr<bridge::Connection> connection_;
    std::string token_;
    std::string application_version_;
    std::uint64_t next_request_id_ = 1;

    // No configured project: discover at call time, connect unbound.
    bool dynamic_ = false;
    // --offline-project: never discover, never connect, never promote.
    bool offline_only_ = false;
    // How many live instances the last dynamic discovery saw. Zero and one
    // explain themselves; more than one is reported rather than auto-picked.
    int last_discovery_count_ = 0;
    // Why the last dynamic connect failed, when an instance was found but not
    // reached -- a protocol mismatch reported as "start Assurance Forge"
    // would send the user to fix the wrong thing.
    std::string last_dynamic_error_;

    std::filesystem::path project_path_;
    // Resolved once at open so every consent read hits the same file.
    std::filesystem::path settings_path_;
    bool initialized_ = false;
    bool never_connect_ = false;
    // True when OpenOffline loaded a copy this session can serve reads from. A
    // session that opened connected has no such copy, and does not load one
    // when the connection drops: the disk may be behind what the user was
    // looking at, and a silently stale answer is worse than an error that says
    // to start the application.
    bool offline_loaded_ = false;
    std::string client_label_;
    std::string session_id_;
};

// Reads the `mcp.enabled` flag from a settings document. A missing file, a
// missing section, a non-boolean value, and a malformed document all read as
// false: a consent gate must fail closed, because the failure mode in the other
// direction is publishing a safety argument to a client the user never approved.
bool ReadMcpConsent(const std::filesystem::path& settings_path);

} // namespace mcp
