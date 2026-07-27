#include "app/controllers/agent_bridge_controller.h"

#include "bridge/endpoint.h"
#include "core/time_utils.h"

#include <chrono>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace app::controllers {
namespace {

// How long a connection thread waits for the frame thread to answer. A frame
// takes milliseconds; this only matters when the application is wedged, and it
// exists so an AI client reports a stalled tool call instead of hanging until
// the user notices. Generous, because a very large `get_argument_tree` on a slow
// machine is a legitimate reason to be slow.
constexpr std::chrono::seconds kFrameResponseTimeout{30};

long long CurrentProcessId() {
#ifdef _WIN32
    return static_cast<long long>(GetCurrentProcessId());
#else
    return static_cast<long long>(::getpid());
#endif
}

} // namespace

struct AgentBridgeController::PendingRequest {
    bridge::Request  request;
    AgentConnection  connection;
    bridge::Response response;
    bool             done = false;
};

AgentBridgeController::~AgentBridgeController() {
    Stop();
}

bool AgentBridgeController::Start(const std::filesystem::path& project_root,
                                  std::string app_version, std::string& error) {
    error.clear();
    if (project_root.empty()) {
        error = "Cannot serve the bridge without an open project.";
        return false;
    }
    if (listening_.load() && project_root_ == project_root) {
        return true;
    }
    // Switching projects: the old listener and its record must go first, or two
    // records would claim this one application.
    Stop();

    const std::string address = bridge::EndpointAddressFor(project_root);
    listener_                 = bridge::Listener::Start(address, error);
    if (listener_ == nullptr) {
        return false;
    }

    project_root_ = project_root;
    app_version_  = std::move(app_version);
    token_        = bridge::GenerateToken();

    bridge::EndpointRecord record;
    record.protocol     = bridge::kProtocolVersion;
    record.pid          = CurrentProcessId();
    record.address      = address;
    record.token        = token_;
    record.project_root = project_root.generic_string();
    record.app_version  = app_version_;
    if (!bridge::WriteEndpointRecord(record, error)) {
        listener_.reset();
        project_root_.clear();
        return false;
    }

    stopping_.store(false);
    listening_.store(true);
    accept_thread_ = std::thread([this] { AcceptLoop(); });
    return true;
}

void AgentBridgeController::Stop() {
    if (listener_ == nullptr && !listening_.load()) {
        return;
    }
    stopping_.store(true);
    listening_.store(false);

    if (listener_ != nullptr) {
        listener_->Stop();
    }

    // Closing each connection is what makes its thread joinable: a thread
    // blocked in `ReadMessage` on a client with nothing to say returns only when
    // the connection ends, and joining it before that would hang the exit.
    std::vector<std::unique_ptr<ServedConnection>> served;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        served.swap(served_);
        // Nothing queued will ever be answered now. Releasing the waiters is
        // what lets their threads finish.
        for (const std::shared_ptr<PendingRequest>& pending : pending_) {
            pending->response = bridge::MakeError(pending->request.id,
                                                  bridge::error_code::kInternal,
                                                  "Assurance Forge is closing this project.");
            pending->done     = true;
        }
        pending_.clear();
    }
    queued_.notify_all();

    for (const std::unique_ptr<ServedConnection>& entry : served) {
        if (entry->connection != nullptr) {
            entry->connection->Close();
        }
    }
    for (const std::unique_ptr<ServedConnection>& entry : served) {
        if (entry->thread.joinable()) {
            entry->thread.join();
        }
    }
    served.clear();

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    if (!project_root_.empty()) {
        bridge::RemoveEndpointRecord(project_root_);
    }
    listener_.reset();
    project_root_.clear();
}

void AgentBridgeController::AcceptLoop() {
    while (!stopping_.load()) {
        std::string                         error;
        std::unique_ptr<bridge::Connection> accepted = listener_->Accept(error);
        if (accepted == nullptr) {
            // A clean stop reports no error; a transport failure reports one.
            // Either ends the loop, because a listener that cannot accept will
            // not start being able to.
            return;
        }

        AgentConnection descriptor;
        descriptor.id            = next_connection_id_.fetch_add(1);
        descriptor.client_label  = "unknown client";
        descriptor.connected_utc = core::NowUtcString();

        std::unique_ptr<ServedConnection> entry = std::make_unique<ServedConnection>();
        entry->descriptor                       = descriptor;
        entry->connection = std::shared_ptr<bridge::Connection>(accepted.release());

        const std::shared_ptr<bridge::Connection> connection = entry->connection;
        entry->thread = std::thread([this, connection, descriptor] {
            ServeConnection(connection, descriptor);
        });

        const std::lock_guard<std::mutex> lock(mutex_);
        served_.push_back(std::move(entry));
    }
}

bool AgentBridgeController::CheckEnvelope(const bridge::Request& request, bool initialized,
                                          bridge::Response& refusal) const {
    if (!bridge::IsSupportedProtocol(request.protocol)) {
        refusal = bridge::MakeError(request.id, bridge::error_code::kUnsupportedProtocol,
                                    bridge::UnsupportedProtocolMessage(request.protocol));
        return false;
    }
    if (request.token != token_) {
        // The endpoint record holding the token lives in the user's own runtime
        // directory. A caller without it did not read that file, so it is not
        // the adapter this application published for.
        refusal = bridge::MakeError(request.id, bridge::error_code::kUnauthorized,
                                    "This connection did not present the token Assurance Forge "
                                    "published for the open project.");
        return false;
    }
    if (!initialized && request.op != bridge::kHelloOperation) {
        refusal = bridge::MakeError(request.id, bridge::error_code::kNotInitialized,
                                    "Send \"hello\" before any other operation.");
        return false;
    }
    return true;
}

void AgentBridgeController::MarkInitialized(std::uint64_t id, const std::string& client_label) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const std::unique_ptr<ServedConnection>& entry : served_) {
        if (entry->descriptor.id == id) {
            entry->descriptor.client_label = client_label;
            return;
        }
    }
}

void AgentBridgeController::ForgetConnection(std::uint64_t id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const std::unique_ptr<ServedConnection>& entry : served_) {
        if (entry->descriptor.id == id) {
            // Marked rather than erased: the thread reporting this is its own
            // thread, and erasing the entry would destroy the object it is
            // running inside.
            entry->connection.reset();
            return;
        }
    }
}

void AgentBridgeController::ServeConnection(std::shared_ptr<bridge::Connection> connection,
                                            AgentConnection descriptor) {
    bool        initialized = false;
    std::string message;
    while (!stopping_.load() && connection->ReadMessage(message)) {
        bridge::Request request;
        std::string     decode_error;
        if (!bridge::DecodeRequest(message, request, decode_error)) {
            connection->WriteMessage(bridge::EncodeResponse(
                bridge::MakeError(0, bridge::error_code::kBadRequest, decode_error)));
            continue;
        }

        bridge::Response refusal;
        if (!CheckEnvelope(request, initialized, refusal)) {
            connection->WriteMessage(bridge::EncodeResponse(refusal));
            // A protocol or token failure is not fixable by retrying on the same
            // connection, and continuing to serve one would keep an
            // unauthorized peer talking.
            if (refusal.error_code != bridge::error_code::kNotInitialized) {
                break;
            }
            continue;
        }

        if (request.op == bridge::kHelloOperation) {
            const nlohmann::json::const_iterator client = request.args.find("client");
            if (client != request.args.end() && client->is_string()) {
                descriptor.client_label = client->get<std::string>();
            }
            MarkInitialized(descriptor.id, descriptor.client_label);
            initialized = true;
            connection->WriteMessage(bridge::EncodeResponse(bridge::MakeResult(
                request.id, nlohmann::json{
                                {"protocol", bridge::kProtocolVersion},
                                {"appVersion", app_version_},
                                {"projectRoot", project_root_.generic_string()},
                                {"connectionId", descriptor.id},
                            })));
            continue;
        }

        const std::shared_ptr<PendingRequest> pending = std::make_shared<PendingRequest>();
        pending->request                              = request;
        pending->connection                           = descriptor;

        {
            const std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(pending);
        }
        queued_.notify_all();

        bridge::Response response;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool answered =
                queued_.wait_for(lock, kFrameResponseTimeout, [&pending] { return pending->done; });
            response = answered ? pending->response
                                : bridge::MakeError(request.id, bridge::error_code::kInternal,
                                                    "Assurance Forge did not answer in time.");
        }
        if (!connection->WriteMessage(bridge::EncodeResponse(response))) {
            break;
        }
    }

    ForgetConnection(descriptor.id);
}

int AgentBridgeController::PollPendingRequests(const OperationHandler& handler) {
    std::vector<std::shared_ptr<PendingRequest>> batch;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        batch.assign(pending_.begin(), pending_.end());
        pending_.clear();
    }
    if (batch.empty()) {
        return 0;
    }

    // Run outside the lock: a handler executes domain code that can take a
    // moment, and holding the queue lock across it would block every connection
    // thread from enqueuing.
    for (const std::shared_ptr<PendingRequest>& pending : batch) {
        pending->response = handler(pending->request, pending->connection);
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const std::shared_ptr<PendingRequest>& pending : batch) {
            pending->done = true;
        }
    }
    queued_.notify_all();
    return static_cast<int>(batch.size());
}

std::vector<AgentConnection> AgentBridgeController::connections() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AgentConnection> live;
    for (const std::unique_ptr<ServedConnection>& entry : served_) {
        // A finished connection keeps its entry so its thread stays joinable,
        // but it is no longer something the user should be told is attached.
        if (entry->connection != nullptr) {
            live.push_back(entry->descriptor);
        }
    }
    return live;
}

} // namespace app::controllers
