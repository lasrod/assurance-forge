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
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace app::controllers {

// One connected AI client. Surfaced so the application can show that something
// is attached to the open project rather than leaving it invisible.
struct AgentConnection {
    std::uint64_t id = 0;
    // From the adapter's handshake, e.g. "claude-ai 0.1.0". Used for
    // attribution on anything the connection produces.
    std::string client_label;
    // Random identity minted by the adapter process. Unlike the application's
    // numeric connection id, it cannot collide with a persisted draft group
    // after an application restart.
    std::string session_id;
    std::string connected_utc;
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

    // Starts listening for `project_root` and publishes the endpoint record so
    // an adapter can find it. Starting for a project already being served is a
    // no-op; starting for a different one stops the previous listener first, so
    // switching projects cannot leave two records pointing at one application.
    bool Start(const std::filesystem::path& project_root, std::string app_version, std::string& error);

    // Stops listening, unwinds every thread, and removes the endpoint record.
    // Safe to call when not listening, and safe to call twice.
    void Stop();

    bool listening() const {
        return listening_.load();
    }
    const std::filesystem::path& project_root() const {
        return project_root_;
    }

    // Runs every queued request through `handler`. Call once per frame.
    // Returns how many ran, so the caller can mark derived views dirty only
    // when something actually happened.
    int PollPendingRequests(const OperationHandler& handler);

    std::vector<AgentConnection> connections() const;

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
    void UpdateConnectionIdentity(std::uint64_t id, const std::string& client_label, const std::string& session_id);
    void ForgetConnection(std::uint64_t id);

    std::unique_ptr<bridge::Listener> listener_;
    std::thread accept_thread_;

    std::filesystem::path project_root_;
    std::string app_version_;
    std::string token_;
    std::atomic<bool> listening_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<uint64_t> next_connection_id_{1};

    mutable std::mutex mutex_;
    std::condition_variable queued_;
    std::deque<std::shared_ptr<PendingRequest>> pending_;
    // Every connection ever served this session, including finished ones whose
    // thread is still joinable. Cleared on `Stop`.
    std::vector<std::unique_ptr<ServedConnection>> served_;
};

} // namespace app::controllers
