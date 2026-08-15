#pragma once

// The application's side of the bridge: it listens, and it runs every agent
// operation on the frame thread.
//
// **Requests execute on the frame thread, not on the connection thread.** That
// is the whole design. The frame thread owns the model, so an operation running
// there sees exactly what the user sees, needs no lock, and cannot observe the
// argument halfway through an edit. It also means an agent's work is visible to
// the same code that draws it.
//
// **The listener outlives the open project** (ADR 0014). It starts once per
// application run, publishes one instance record keyed by a runtime instance
// id, and survives project open/close/switch; only the record's project
// fingerprint changes. A connection is bound to the project fingerprint its
// hello named, and an operation whose project is no longer the active one is
// refused with `project_not_active` rather than executed against whatever the
// user switched to -- the guard that keeps a project switch from silently
// retargeting a session.
//
// The pattern -- background thread, queue, drain once per frame -- is the one
// `ai::AiTaskRunner` and `AppRuntime::PollAiReviewTask` already use for provider
// calls, for the same reason.
//
// Nothing here interprets or directly writes assurance data. Reads answer from
// the live working model; draft mutations are revision-checked on the frame
// thread and human promotion remains the ordinary audited command path.

#include "bridge/protocol.h"
#include "bridge/transport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace app::controllers {

// One connected AI client. Surfaced so the application can show that something
// is attached to the open project rather than leaving it invisible.
struct AgentConnection {
    std::uint64_t id = 0;
    // From the adapter's handshake, e.g. "claude-ai 0.1.0". Used for
    // attribution on anything the connection produces -- display only, never
    // authentication.
    std::string client_label;
    // Random identity minted by the adapter process. Unlike the application's
    // numeric connection id, it cannot collide with a persisted draft group
    // after an application restart.
    std::string session_id;
    // The project fingerprint the connection's hello named. Operations run
    // only while this is the active project's fingerprint.
    std::string project_key;
    std::string connected_utc;
    // Derived when listed: this session's access state against the active
    // project -- "granted", "pending", "denied", or "none".
    std::string access_state;
};

// One session waiting for the user's decision (ADR 0014 gate 2).
struct AccessRequest {
    std::string session_id;
    std::string client_label;
    // The active project's fingerprint at request time. A request outlives
    // neither the project it named nor the application run.
    std::string project_key;
    std::string requested_utc;
};

class AgentBridgeController {
public:
    // Executes one operation against the live model. Called on the frame
    // thread, once per queued request.
    using OperationHandler = std::function<bridge::Response(const bridge::Request&, const AgentConnection&)>;

    AgentBridgeController() = default;
    ~AgentBridgeController();

    AgentBridgeController(const AgentBridgeController&) = delete;
    AgentBridgeController& operator=(const AgentBridgeController&) = delete;

    // Starts the listener, mints the instance id and token, and publishes the
    // instance record. Independent of any project; starting while already
    // listening is a no-op. Prunes records left behind by crashed instances.
    bool Start(std::string app_version, std::string& error);

    // Publishes which project this instance has open (an empty path means
    // none). The listener is untouched. Changing project answers every queued
    // request with a refusal first, so nothing staged against the old project
    // can execute against the new one.
    void SetActiveProject(const std::filesystem::path& project_root);

    // Refreshes the record's heartbeat timestamp, at most once per interval.
    // Call once per frame; almost every call is a cheap no-op.
    void WriteHeartbeatIfDue();

    // Stops listening, unwinds every thread, and removes the instance record.
    // Safe to call when not listening, and safe to call twice.
    void Stop();

    bool listening() const {
        return listening_.load();
    }
    const std::string& instance_id() const {
        return instance_id_;
    }
    std::filesystem::path active_project_root() const;

    // Runs every queued request through `handler`. Call once per frame.
    // Returns how many ran, so the caller can mark derived views dirty only
    // when something actually happened. Requests whose connection is bound to
    // a project that is not active are answered here with `project_not_active`
    // and never reach the handler.
    int PollPendingRequests(const OperationHandler& handler);

    std::vector<AgentConnection> connections() const;

    // ADR 0014 gate 2: per-session, per-project grants held in application
    // memory only. They survive a bridge reconnect to the same instance (the
    // key is the adapter's stable session id) and end on revoke, deny,
    // project close or switch, MCP disable, or application restart.
    std::vector<AccessRequest> PendingAccessRequests() const;
    // Grant or deny the pending request of `session_id` against the active
    // project. A grant lasts while the project stays open.
    void GrantAccess(const std::string& session_id);
    void DenyAccess(const std::string& session_id);
    // Removes a session's grants. The next operation raises a fresh request.
    void RevokeAccess(const std::string& session_id);
    // Every grant, denial and pending request. The Preferences MCP toggle
    // calls this when the master gate closes.
    void RevokeAllAccess();

private:
    struct PendingRequest;

    // A connection and the thread serving it. The connection is held so that
    // `Stop` can close it: a thread blocked in `ReadMessage` waiting on a client
    // that has nothing to say would otherwise never return, and joining it would
    // hang the application on exit.
    struct ServedConnection {
        AgentConnection descriptor;
        std::shared_ptr<bridge::Connection> connection;
        std::thread thread;
    };

    void AcceptLoop();
    void ServeConnection(std::shared_ptr<bridge::Connection> connection, AgentConnection descriptor);
    // Protocol version, token and handshake ordering, checked on the connection
    // thread so the frame thread only ever sees requests worth running.
    bool CheckEnvelope(const bridge::Request& request, bool initialized, bridge::Response& refusal) const;
    void UpdateConnectionIdentity(const AgentConnection& descriptor);
    void ForgetConnection(std::uint64_t id);
    // The record as it should read right now. Callers hold no lock; the
    // fields it reads are only written on the frame thread.
    bool PublishRecord(std::string& error);
    std::string ActiveProjectKey() const;

    std::unique_ptr<bridge::Listener> listener_;
    std::thread accept_thread_;

    std::string instance_id_;
    std::string app_version_;
    std::string token_;
    std::atomic<bool> listening_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<uint64_t> next_connection_id_{1};
    std::chrono::steady_clock::time_point last_heartbeat_{};

    // Access bookkeeping, guarded by mutex_. Keys are session_id + "\n" +
    // project_key -- both components are free of newlines by construction.
    std::string AccessKey(const std::string& session_id, const std::string& project_key) const;
    // Appends a request if the session has none pending. Caller holds mutex_.
    void RegisterAccessRequestLocked(const AgentConnection& connection);

    mutable std::mutex mutex_;
    // Guarded by mutex_: written on the frame thread, read by connection
    // threads answering hello.
    std::filesystem::path active_project_root_;
    std::string active_project_key_;
    std::vector<AccessRequest> pending_access_;
    std::set<std::string> granted_access_;
    std::set<std::string> denied_access_;
    std::condition_variable queued_;
    std::deque<std::shared_ptr<PendingRequest>> pending_;
    // Every connection ever served this session, including finished ones whose
    // thread is still joinable. Cleared on `Stop`.
    std::vector<std::unique_ptr<ServedConnection>> served_;
};

} // namespace app::controllers
