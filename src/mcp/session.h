#pragma once

// One MCP connection's view of an Assurance Forge project, in one of two modes.
//
// **Connected.** Assurance Forge has this project open. The session holds no
// model of its own; every operation goes over the bridge and is answered by the
// application from the argument the user is looking at. This is the mode that
// matters: the agent and the user see one thing, and anything that changes the
// case goes through the same command, validation, undo and audit path as an
// edit made with the mouse.
//
// **Offline.** No application is running, so the session loads the project
// itself and answers reads from that copy. It is **read-only**. Proposing a
// change needs a command bus, an audit log and a human to accept it, none of
// which exist in a headless process -- and a second writer in the project
// directory is exactly the fault this design removes.
//
// The mode is decided once, at open. A session does not promote itself when the
// application starts later: the client has already been told what this
// connection can do, and quietly changing that mid-conversation is worse than
// asking the user to reconnect.

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
        // Answering from a copy this process loaded. Reads only.
        Offline,
    };

    struct Config {
        // A project directory, a project manifest, or a bare SACM file.
        std::filesystem::path project_path;
        // Empty resolves to core::UserSettingsFilePath(). Tests point this at a
        // temporary file so a developer's real consent setting cannot make a
        // test pass that would fail on a clean machine.
        std::filesystem::path settings_path;
        // Tests set this to keep a session offline even when the developer
        // happens to have the project open in Assurance Forge.
        bool never_connect = false;
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
        // True when the operation failed because this session is offline, so a
        // caller can say so rather than reporting a generic failure.
        bool needs_application = false;
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
    void set_client_label(std::string label) {
        client_label_ = std::move(label);
    }
    const std::string& client_label() const {
        return client_label_;
    }

    const std::filesystem::path& project_path() const {
        return project_path_;
    }

private:
    Session() = default;

    bool ConnectToApplication(std::string& error);
    bool OpenOffline(std::string& error);
    OperationResult RunOverBridge(const std::string& op, const nlohmann::json& args);
    OperationResult RunOffline(const std::string& op, const nlohmann::json& args);

    Mode mode_ = Mode::Offline;
    core::AppState state_;
    std::unique_ptr<bridge::Connection> connection_;
    std::string token_;
    std::string application_version_;
    std::uint64_t next_request_id_ = 1;

    std::filesystem::path project_path_;
    // Resolved once at open so every consent read hits the same file.
    std::filesystem::path settings_path_;
    bool initialized_ = false;
    std::string client_label_;
};

// Reads the `mcp.enabled` flag from a settings document. A missing file, a
// missing section, a non-boolean value, and a malformed document all read as
// false: a consent gate must fail closed, because the failure mode in the other
// direction is publishing a safety argument to a client the user never approved.
bool ReadMcpConsent(const std::filesystem::path& settings_path);

} // namespace mcp
