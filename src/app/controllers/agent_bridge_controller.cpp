#include "app/controllers/agent_bridge_controller.h"

#include "bridge/instance_registry.h"
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

// How often the instance record's heartbeat is refreshed. Liveness is the pid,
// not this timestamp -- the heartbeat exists so a human inspecting the record
// can see the instance is not merely alive but attended.
constexpr std::chrono::seconds kHeartbeatInterval{15};

constexpr const char* kProjectNotActiveMessage =
    "Assurance Forge no longer has this session's project open. Reads and draft "
    "operations resume automatically when the user opens it again; nothing from "
    "the newly opened project is shared with this session.";

constexpr const char* kProjectAccessRequiredMessage =
    "This session is not bound to a project and has no access grant, so project "
    "content is not served to it. Until access grants are available, launch "
    "assurance-forge-mcp with --project <path> to bind the session to a project.";

long long CurrentProcessId() {
#ifdef _WIN32
    return static_cast<long long>(GetCurrentProcessId());
#else
    return static_cast<long long>(::getpid());
#endif
}

} // namespace

struct AgentBridgeController::PendingRequest {
    bridge::Request request;
    AgentConnection connection;
    bridge::Response response;
    bool done = false;
};

AgentBridgeController::~AgentBridgeController() {
    Stop();
}

bool AgentBridgeController::Start(std::string app_version, std::string& error) {
    error.clear();
    if (listening_.load()) {
        return true;
    }

    // A crashed instance leaves its record behind; whoever starts next sweeps
    // it up so enumeration stays honest.
    bridge::PruneStaleInstanceRecords();

    instance_id_ = bridge::GenerateInstanceId();
    const std::string address = bridge::InstanceAddress(instance_id_);
    listener_ = bridge::Listener::Start(address, error);
    if (listener_ == nullptr) {
        instance_id_.clear();
        return false;
    }

    app_version_ = std::move(app_version);
    token_ = bridge::GenerateToken();

    if (!PublishRecord(error)) {
        listener_.reset();
        instance_id_.clear();
        return false;
    }
    last_heartbeat_ = std::chrono::steady_clock::now();

    stopping_.store(false);
    listening_.store(true);
    accept_thread_ = std::thread([this] { AcceptLoop(); });
    return true;
}

bool AgentBridgeController::PublishRecord(std::string& error) {
    bridge::InstanceRecord record;
    record.protocol = bridge::kProtocolVersion;
    record.instance_id = instance_id_;
    record.pid = CurrentProcessId();
    record.address = listener_ != nullptr ? listener_->address() : bridge::InstanceAddress(instance_id_);
    record.token = token_;
    record.app_version = app_version_;
    const std::string project_key = ActiveProjectKey();
    record.state = project_key.empty() ? bridge::instance_state::kNoProject : bridge::instance_state::kProjectOpen;
    record.project_key = project_key;
    record.last_heartbeat_utc = core::NowUtcString();
    return bridge::WriteInstanceRecord(record, error);
}

std::string AgentBridgeController::ActiveProjectKey() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return active_project_key_;
}

std::filesystem::path AgentBridgeController::active_project_root() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return active_project_root_;
}

void AgentBridgeController::SetActiveProject(const std::filesystem::path& project_root) {
    const std::string new_key = project_root.empty() ? std::string() : bridge::ProjectKey(project_root);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (active_project_key_ == new_key) {
            active_project_root_ = project_root;
            return;
        }
        active_project_root_ = project_root;
        active_project_key_ = new_key;

        // Anything queued was staged against the previous project. Refusing it
        // here is what keeps a request from executing against a model it was
        // never aimed at.
        for (const std::shared_ptr<PendingRequest>& pending : pending_) {
            pending->response =
                bridge::MakeError(pending->request.id, bridge::error_code::kProjectNotActive, kProjectNotActiveMessage);
            pending->done = true;
        }
        pending_.clear();
    }
    queued_.notify_all();

    if (listening_.load()) {
        std::string error;
        PublishRecord(error);
        // A failed record update is not worth failing a project open over; the
        // stale record corrects itself on the next heartbeat.
    }
}

void AgentBridgeController::WriteHeartbeatIfDue() {
    if (!listening_.load()) {
        return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - last_heartbeat_ < kHeartbeatInterval) {
        return;
    }
    last_heartbeat_ = now;
    std::string error;
    PublishRecord(error);
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
            pending->response = bridge::MakeError(
                pending->request.id, bridge::error_code::kInternal, "Assurance Forge is shutting down.");
            pending->done = true;
        }
        pending_.clear();
        active_project_root_.clear();
        active_project_key_.clear();
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

    if (!instance_id_.empty()) {
        bridge::RemoveInstanceRecord(instance_id_);
    }
    listener_.reset();
    instance_id_.clear();
}

void AgentBridgeController::AcceptLoop() {
    while (!stopping_.load()) {
        std::string error;
        std::unique_ptr<bridge::Connection> accepted = listener_->Accept(error);
        if (accepted == nullptr) {
            // A clean stop reports no error; a transport failure reports one.
            // Either ends the loop, because a listener that cannot accept will
            // not start being able to.
            return;
        }

        AgentConnection descriptor;
        descriptor.id = next_connection_id_.fetch_add(1);
        descriptor.client_label = "unknown client";
        descriptor.connected_utc = core::NowUtcString();

        std::unique_ptr<ServedConnection> entry = std::make_unique<ServedConnection>();
        entry->descriptor = descriptor;
        entry->connection = std::shared_ptr<bridge::Connection>(accepted.release());

        const std::shared_ptr<bridge::Connection> connection = entry->connection;
        entry->thread = std::thread([this, connection, descriptor] { ServeConnection(connection, descriptor); });

        const std::lock_guard<std::mutex> lock(mutex_);
        served_.push_back(std::move(entry));
    }
}

bool AgentBridgeController::CheckEnvelope(const bridge::Request& request,
                                          bool initialized,
                                          bridge::Response& refusal) const {
    if (!bridge::IsSupportedProtocol(request.protocol)) {
        refusal = bridge::MakeError(
            request.id, bridge::error_code::kUnsupportedProtocol, bridge::UnsupportedProtocolMessage(request.protocol));
        return false;
    }
    if (request.token != token_) {
        // The instance record holding the token lives in the user's own runtime
        // directory. A caller without it did not read that file, so it is not
        // the adapter this application published for.
        refusal = bridge::MakeError(request.id,
                                    bridge::error_code::kUnauthorized,
                                    "This connection did not present the token Assurance Forge "
                                    "published for this instance.");
        return false;
    }
    if (!initialized && request.op != bridge::kHelloOperation) {
        refusal = bridge::MakeError(
            request.id, bridge::error_code::kNotInitialized, "Send \"hello\" before any other operation.");
        return false;
    }
    return true;
}

void AgentBridgeController::UpdateConnectionIdentity(const AgentConnection& descriptor) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const std::unique_ptr<ServedConnection>& entry : served_) {
        if (entry->descriptor.id == descriptor.id) {
            entry->descriptor = descriptor;
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
    bool initialized = false;
    std::string message;
    while (!stopping_.load() && connection->ReadMessage(message)) {
        bridge::Request request;
        std::string decode_error;
        if (!bridge::DecodeRequest(message, request, decode_error)) {
            connection->WriteMessage(
                bridge::EncodeResponse(bridge::MakeError(0, bridge::error_code::kBadRequest, decode_error)));
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
            const nlohmann::json::const_iterator session = request.args.find("session");
            if (session == request.args.end() || !session->is_string() || session->get<std::string>().empty()) {
                connection->WriteMessage(bridge::EncodeResponse(bridge::MakeError(
                    request.id, bridge::error_code::kBadRequest, "Hello requires a non-empty session id.")));
                continue;
            }
            // The project fingerprint is optional: a dynamic session connects
            // unbound and receives no project content until a grant binds it.
            // When present it must name the active project -- bound at hello,
            // never rebound (ADR 0014). Present-but-malformed is a client bug
            // and is refused rather than silently downgraded to unbound.
            const nlohmann::json::const_iterator project_key = request.args.find("projectKey");
            const bool wants_binding = project_key != request.args.end();
            if (wants_binding && (!project_key->is_string() || project_key->get<std::string>().empty())) {
                connection->WriteMessage(bridge::EncodeResponse(
                    bridge::MakeError(request.id,
                                      bridge::error_code::kBadRequest,
                                      "projectKey, when present, must be a non-empty string; omit it entirely "
                                      "for an unbound connection.")));
                continue;
            }

            std::filesystem::path active_root;
            bool project_open = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (wants_binding && project_key->get<std::string>() != active_project_key_) {
                    connection->WriteMessage(bridge::EncodeResponse(bridge::MakeError(
                        request.id, bridge::error_code::kProjectNotActive, kProjectNotActiveMessage)));
                    continue;
                }
                active_root = active_project_root_;
                project_open = !active_project_key_.empty();
            }

            const nlohmann::json::const_iterator client = request.args.find("client");
            if (client != request.args.end() && client->is_string()) {
                descriptor.client_label = client->get<std::string>();
            }
            descriptor.session_id = session->get<std::string>();
            descriptor.project_key = wants_binding ? project_key->get<std::string>() : std::string();
            UpdateConnectionIdentity(descriptor);
            initialized = true;
            nlohmann::json greeting{
                {"protocol", bridge::kProtocolVersion},
                {"appVersion", app_version_},
                {"instanceId", instance_id_},
                {"connectionId", descriptor.id},
            };
            if (wants_binding) {
                // The root is the bound session's own configuration echoed
                // back. An unbound session gets only the coarse fact that a
                // project is open -- which project stays undisclosed until a
                // grant exists.
                greeting["projectRoot"] = active_root.generic_string();
            } else {
                greeting["projectOpen"] = project_open;
            }
            connection->WriteMessage(bridge::EncodeResponse(bridge::MakeResult(request.id, greeting)));
            continue;
        }

        if (request.op == bridge::kIdentifyOperation) {
            const nlohmann::json::const_iterator client = request.args.find("client");
            if (client == request.args.end() || !client->is_string() || client->get<std::string>().empty()) {
                connection->WriteMessage(bridge::EncodeResponse(bridge::MakeError(
                    request.id, bridge::error_code::kBadRequest, "Identify requires a non-empty client label.")));
                continue;
            }
            descriptor.client_label = client->get<std::string>();
            UpdateConnectionIdentity(descriptor);
            connection->WriteMessage(bridge::EncodeResponse(
                bridge::MakeResult(request.id, nlohmann::json{{"client", descriptor.client_label}})));
            continue;
        }

        const std::shared_ptr<PendingRequest> pending = std::make_shared<PendingRequest>();
        pending->request = request;
        pending->connection = descriptor;

        {
            const std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(pending);
        }
        queued_.notify_all();

        bridge::Response response;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool answered = queued_.wait_for(lock, kFrameResponseTimeout, [&pending] { return pending->done; });
            response = answered ? pending->response
                                : bridge::MakeError(request.id,
                                                    bridge::error_code::kInternal,
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
    std::string active_key;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        batch.assign(pending_.begin(), pending_.end());
        pending_.clear();
        active_key = active_project_key_;
    }
    if (batch.empty()) {
        return 0;
    }

    // Run outside the lock: a handler executes domain code that can take a
    // moment, and holding the queue lock across it would block every connection
    // thread from enqueuing.
    for (const std::shared_ptr<PendingRequest>& pending : batch) {
        if (pending->connection.project_key.empty()) {
            // An unbound connection never reaches the handler, even when no
            // project is open: the master consent flag alone must not
            // disclose whatever the user opens next (ADR 0014 gate 2).
            pending->response = bridge::MakeError(
                pending->request.id, bridge::error_code::kProjectAccessRequired, kProjectAccessRequiredMessage);
            continue;
        }
        if (pending->connection.project_key != active_key) {
            // Checked here, on the frame thread that owns the active project,
            // so the decision and the execution cannot interleave with a
            // project switch. The connection stays open: the refusal is
            // recoverable the moment the user reopens the project.
            pending->response =
                bridge::MakeError(pending->request.id, bridge::error_code::kProjectNotActive, kProjectNotActiveMessage);
            continue;
        }
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
