#include "mcp/session.h"

#include "agent/operations.h"
#include "bridge/instance_registry.h"
#include "core/user_settings.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <thread>

namespace mcp {
namespace {

// Opening a project reads its manifest but does not load any of its documents,
// so an offline session pointed at a project directory would otherwise come up
// with no assurance case at all. Opens the project's first argument file.
//
// Which argument the *user* has open is a property of the running application,
// and offline there is no running application to ask. `open_case_file` lets a
// client move between them.
bool OpenProjectArgumentFile(core::AppState& state) {
    if (!state.current_project.has_value()) {
        return false;
    }
    for (const core::ProjectFileEntry& entry : state.current_project->files) {
        if (entry.role == core::ProjectFileRole::SacmArgument) {
            return state.open_project_file(entry);
        }
    }
    return false;
}

// A path we should hand to AppState::open_project rather than load_file: a
// directory, or a manifest. Bare SACM files load standalone, which is useful for
// inspecting a file that is not part of a project, but yields no project root.
bool LooksLikeAssuranceCaseFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension == ".sacm" || extension == ".xml";
}

// Reads only. The offline copy never opens the application's integrated draft,
// because it cannot show unaccepted changes to a human or serialize concurrent
// contributors through the workspace revision.
bool IsOfflineOperation(const std::string& op) {
    return op == "get_case_overview" || op == "find_elements" || op == "get_element" || op == "get_argument_tree" ||
           op == "list_case_files" || op == "open_case_file" || op == "suggest_placement" ||
           op == "list_assurance_claim_points" || op == "list_terms";
}

} // namespace

bool ReadMcpConsent(const std::filesystem::path& settings_path) {
    // Shares the reader with the Preferences toggle that writes it, so the gate
    // and the switch can never disagree about what "enabled" means. Every failure
    // path -- missing file, unreadable, malformed, absent or non-boolean flag --
    // resolves to false.
    return core::LoadMcpUserSettings(settings_path).enabled;
}

bool Session::consent_granted() const {
    return ReadMcpConsent(settings_path_);
}

Session::~Session() = default;

void Session::set_client_label(std::string label) {
    client_label_ = std::move(label);
    if (!connected() || connection_ == nullptr)
        return;

    bridge::Request identify;
    identify.id = next_request_id_++;
    identify.op = bridge::kIdentifyOperation;
    identify.token = token_;
    identify.args = nlohmann::json{{"client", client_label_}};

    std::string reply;
    // A failed identity refresh is treated like any other lost bridge
    // connection by the next tool call. Initialization itself still succeeds,
    // so the MCP client receives the actionable connection error on use rather
    // than a malformed initialize response.
    if (!connection_->WriteMessage(bridge::EncodeRequest(identify)) || !connection_->ReadMessage(reply))
        return;
}

bool Session::ConnectToApplication(std::string& error) {
    // Discovery is by running instance, not by project (ADR 0014): every
    // instance publishes one record. A project-bound session picks the live
    // instance whose project fingerprint matches the project it was launched
    // for; a dynamic session connects to the single running instance,
    // unbound. Stale records are pruned on the way.
    bridge::InstanceRecord record;
    if (dynamic_) {
        std::vector<bridge::InstanceRecord> live = bridge::EnumerateLiveInstanceRecords();
        last_discovery_count_ = static_cast<int>(live.size());
        if (live.empty()) {
            error = "No running Assurance Forge was found.";
            return false;
        }
        if (live.size() > 1u) {
            // Never auto-select. Choosing the newest is choosing a safety
            // case by timestamp; the explicit selection flow arrives with the
            // access grants.
            error = "More than one Assurance Forge is running. This session never picks one by itself.";
            return false;
        }
        record = std::move(live.front());
    } else {
        if (!bridge::FindInstanceForProjectKey(project_key_, record, error)) {
            return false;
        }
    }
    return ConnectToInstance(record, error);
}

bool Session::ConnectToInstance(const bridge::InstanceRecord& record, std::string& error) {
    if (!bridge::IsSupportedProtocol(record.protocol)) {
        error = bridge::UnsupportedProtocolMessage(record.protocol);
        return false;
    }

    connection_ = bridge::Connection::Connect(record.address, error);
    if (connection_ == nullptr) {
        // A record with nothing listening is the signature of an application
        // that crashed or was killed. Not an error worth reporting to the user:
        // it means the same thing as no record at all.
        return false;
    }
    token_ = record.token;

    bridge::Request hello;
    hello.id = next_request_id_++;
    hello.op = bridge::kHelloOperation;
    hello.token = token_;
    hello.args = nlohmann::json{{"client", client_label_.empty() ? "assurance-forge-mcp" : client_label_},
                                {"session", session_id_}};
    if (!dynamic_) {
        hello.args["projectKey"] = project_key_;
    }

    std::string reply;
    if (!connection_->WriteMessage(bridge::EncodeRequest(hello)) || !connection_->ReadMessage(reply)) {
        error = "Assurance Forge accepted the connection but did not complete a handshake.";
        connection_.reset();
        return false;
    }

    bridge::Response response;
    std::string decode_error;
    if (!bridge::DecodeResponse(reply, response, decode_error) || !response.ok) {
        error = response.error_message.empty() ? decode_error : response.error_message;
        connection_.reset();
        return false;
    }

    application_version_ = response.result.value("appVersion", std::string());
    return true;
}

bool Session::OpenOffline(std::string& error) {
    std::error_code ec;
    const bool is_directory = std::filesystem::is_directory(project_path_, ec);
    const bool opened = (is_directory || !LooksLikeAssuranceCaseFile(project_path_))
                            ? state_.open_project(project_path_.string())
                            : state_.load_file(project_path_.string());
    if (!opened) {
        error = state_.status_message.empty() ? ("Could not open " + project_path_.string()) : state_.status_message;
        return false;
    }

    if (state_.current_project.has_value() && !state_.loaded_case.has_value() && !OpenProjectArgumentFile(state_)) {
        // Not fatal: the project may legitimately hold no argument yet, and a
        // session that can still describe the project is more useful than none.
        // The read tools report the absence rather than pretending to an empty
        // case.
        state_.load_warnings.push_back(
            "The project's assurance case could not be opened: " +
            (state_.status_message.empty() ? std::string("no SACM argument file") : state_.status_message));
    }
    return true;
}

std::unique_ptr<Session> Session::Open(Config config, std::string& error) {
    error.clear();

    if (config.offline_only && config.project_path.empty()) {
        error = "Explicit offline mode needs a path. Pass --offline-project <path>.";
        return nullptr;
    }

    std::unique_ptr<Session> session(new Session());
    session->project_path_ = config.project_path;
    session->settings_path_ = config.settings_path.empty() ? core::UserSettingsFilePath() : config.settings_path;
    session->session_id_ = bridge::GenerateToken();
    session->never_connect_ = config.never_connect || config.offline_only;
    session->offline_only_ = config.offline_only;
    if (!config.project_path.empty()) {
        session->project_key_ = bridge::ProjectKey(config.project_path);
    }

    // Dynamic mode: no configured project. The session must initialize even
    // with no application running -- MCP clients launch this process at client
    // startup -- so a failed first discovery is a state, not an error.
    if (config.project_path.empty()) {
        session->dynamic_ = true;
        std::string connect_error;
        if (!config.never_connect && session->ConnectToApplication(connect_error)) {
            session->mode_ = Mode::Connected;
        } else {
            session->last_dynamic_error_ = connect_error;
            session->mode_ = Mode::Offline;
        }
        return session;
    }

    std::error_code ec;
    if (!std::filesystem::exists(config.project_path, ec)) {
        error = "Project path does not exist: " + config.project_path.string();
        return nullptr;
    }

    if (config.offline_only) {
        session->mode_ = Mode::Offline;
        if (!session->OpenOffline(error)) {
            return nullptr;
        }
        session->offline_loaded_ = true;
        return session;
    }

    // The running application first. It has the argument the user is looking at,
    // including edits that have not reached disk, so any copy this process could
    // load is at best a slightly older version of the truth.
    std::string connect_error;
    if (!config.never_connect && session->ConnectToApplication(connect_error)) {
        session->mode_ = Mode::Connected;
        return session;
    }

    session->mode_ = Mode::Offline;
    if (!session->OpenOffline(error)) {
        return nullptr;
    }
    session->offline_loaded_ = true;
    return session;
}

bool Session::EnsureConnected() {
    if (connection_ != nullptr) {
        return true;
    }
    if (never_connect_) {
        return false;
    }

    if (dynamic_) {
        // Discovery is one directory enumeration; no retry loop. Absent is
        // the ordinary case and must stay free, and mid-restart heals on the
        // caller's next call.
        std::string error;
        if (ConnectToApplication(error)) {
            last_dynamic_error_.clear();
            mode_ = Mode::Connected;
            return true;
        }
        last_dynamic_error_ = error;
        mode_ = Mode::Offline;
        return false;
    }

    // Up to three attempts, ~50ms apart, but only while a live instance has
    // the project open: an application that is mid-restart is reachable again
    // within a breath, and one refused connect should not cost the caller a
    // whole conversational turn. No matching instance fails immediately --
    // "not running" is the ordinary case and it must stay free, or every
    // offline call pays the backoff.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        bridge::InstanceRecord record;
        std::string error;
        if (!bridge::FindInstanceForProjectKey(project_key_, record, error)) {
            break;
        }
        if (ConnectToInstance(record, error)) {
            mode_ = Mode::Connected;
            return true;
        }
    }
    mode_ = Mode::Offline;
    return false;
}

Session::OperationResult Session::DescribeConnection() {
    // One consent read serves both fields; the flag is re-read per call anyway.
    const bool consent = consent_granted();
    OperationResult result;
    result.payload = nlohmann::json{
        {"project_path", project_path_.string()},
        {"consent_granted", consent},
    };
    if (dynamic_) {
        if (connection_ != nullptr) {
            result.payload["mode"] = "connected_unbound";
            result.payload["application_version"] = application_version_;
            result.payload["detail"] =
                "Connected to a running Assurance Forge. Project content requires the user's per-session "
                "grant: call request_project_access to see this session's state -- granted means work "
                "proceeds, pending means the approval is in front of the user, denied means they said no.";
        } else if (last_discovery_count_ > 1) {
            result.payload["mode"] = "multiple_applications";
            result.payload["detail"] = DynamicUnavailableDetail();
        } else if (last_discovery_count_ == 1) {
            result.payload["mode"] = "application_unreachable";
            result.payload["detail"] = DynamicUnavailableDetail();
        } else {
            result.payload["mode"] = "application_unavailable";
            result.payload["detail"] = DynamicUnavailableDetail();
        }
    } else if (offline_only_) {
        result.payload["mode"] = "offline_read_only";
        result.payload["detail"] = "Explicit offline mode (--offline-project). Reads serve the accepted case "
                                   "this process loaded from disk; draft tools are refused, and the session "
                                   "never connects to a running application.";
    } else if (connection_ != nullptr) {
        result.payload["mode"] = "connected";
        result.payload["application_version"] = application_version_;
        result.payload["detail"] = "Connected to a running Assurance Forge. Reads and draft tools answer from "
                                   "the integrated working draft the user is looking at, once the user has "
                                   "granted this session access -- ungranted calls return "
                                   "project_access_pending while the approval is in front of the user.";
    } else if (offline_loaded_) {
        result.payload["mode"] = "offline_read_only";
        result.payload["detail"] = "No running Assurance Forge is reachable, so reads serve the accepted case "
                                   "this process loaded from disk and draft tools are refused. The session "
                                   "connects automatically on the next call once the application has the "
                                   "project open.";
    } else {
        result.payload["mode"] = "unreachable";
        result.payload["detail"] = "The connection to Assurance Forge was lost and this session holds no "
                                   "offline copy. Start Assurance Forge and open the project; the session "
                                   "reconnects automatically on the next call.";
    }
    if (!consent) {
        result.payload["consent_detail"] =
            "MCP sharing is disabled, so tools that return case content will be refused. Enable \"Allow AI "
            "clients to read and propose changes\" in Assurance Forge's Preferences, under MCP Server.";
    }
    return result;
}

Session::OperationResult Session::RunOverBridge(const std::string& op, const nlohmann::json& args) {
    OperationResult result;

    bridge::Request request;
    request.id = next_request_id_++;
    request.op = op;
    request.token = token_;
    request.args = args;

    std::string reply;
    if (!connection_->WriteMessage(bridge::EncodeRequest(request)) || !connection_->ReadMessage(reply)) {
        // The peer is gone. Drop the dead connection so the caller's reconnect
        // path starts clean; Run decides what to tell the client.
        connection_.reset();
        result.is_error = true;
        result.connection_lost = true;
        return result;
    }

    bridge::Response response;
    std::string decode_error;
    if (!bridge::DecodeResponse(reply, response, decode_error)) {
        result.is_error = true;
        result.payload = nlohmann::json{{"error",
                                         "Assurance Forge sent a reply this version "
                                         "could not read: " +
                                             decode_error}};
        return result;
    }
    if (!response.ok) {
        result.is_error = true;
        // The stable code travels with the prose: tool guidance tells clients
        // to branch on values like project_access_pending, so they must be
        // able to see them rather than pattern-match sentences.
        result.payload = nlohmann::json{{"error", response.error_message}};
        if (!response.error_code.empty()) {
            result.payload["error_code"] = response.error_code;
        }
        if (response.error_code == bridge::error_code::kProjectAccessDenied) {
            // The user said no. Remembered so this session's offline copy
            // cannot serve the declined content the moment the application
            // exits.
            denied_by_user_ = true;
        }
        return result;
    }

    // A successful operation means the session is granted -- except the
    // access-status query, which succeeds while reporting any state; clearing
    // on it would let a client launder a denial away by polling. It carries
    // the authoritative answer instead.
    if (op == bridge::kRequestProjectAccessOperation) {
        const std::string status = response.result.value("status", std::string());
        if (status == bridge::access_state::kGranted) {
            denied_by_user_ = false;
        } else if (status == bridge::access_state::kDenied) {
            denied_by_user_ = true;
        }
    } else {
        denied_by_user_ = false;
    }

    // The application marks a domain failure inside a successful response, the
    // same distinction MCP draws between a tool error and a protocol error.
    result.payload = response.result;
    result.is_error = response.result.value("isError", false);
    result.payload.erase("isError");
    return result;
}

Session::OperationResult Session::RunOffline(const std::string& op, const nlohmann::json& args) {
    OperationResult result;

    if (!IsOfflineOperation(op)) {
        result.is_error = true;
        result.needs_application = true;
        result.payload = nlohmann::json{
            {"error",
             offline_only_ ? "This needs Assurance Forge to be running with the project open, and this session was "
                             "launched in explicit offline mode (--offline-project), which never connects. Restart "
                             "the session without --offline-project to work against the running application."
                           : "This needs Assurance Forge to be running with the project open. The running "
                             "application is the sole owner of the integrated draft, shows every staged "
                             "change to the user, and enforces revision checks between contributors. A "
                             "headless copy cannot safely do that. Open the project in Assurance Forge and "
                             "retry; the session connects automatically on the next call."}};
        return result;
    }

    const agent::ReadContext context{state_, project_path_.string()};

    if (op == "get_case_overview") {
        const agent::Result value = agent::GetCaseOverview(context);
        return OperationResult{value.payload, value.is_error, false};
    }
    if (op == "find_elements") {
        const agent::Result value = agent::FindElements(context, args);
        return OperationResult{value.payload, value.is_error, false};
    }
    if (op == "get_element") {
        const agent::Result value = agent::GetElement(context, args);
        return OperationResult{value.payload, value.is_error, false};
    }
    if (op == "list_terms") {
        const agent::Result value = agent::ListTerms(context);
        return OperationResult{value.payload, value.is_error, false};
    }
    if (op == "list_assurance_claim_points") {
        const agent::Result value = agent::ListAssuranceClaimPoints(context);
        return OperationResult{value.payload, value.is_error, false};
    }
    if (op == "get_argument_tree") {
        const agent::Result value = agent::GetArgumentTree(context, args);
        return OperationResult{value.payload, value.is_error, false};
    }
    if (op == "list_case_files") {
        const agent::Result value = agent::ListCaseFiles(context);
        return OperationResult{value.payload, value.is_error, false};
    }
    if (op == "suggest_placement") {
        const agent::Result value = agent::SuggestPlacement(context, args);
        return OperationResult{value.payload, value.is_error, false};
    }

    // open_case_file: switching which argument this offline copy reads.
    const nlohmann::json::const_iterator path = args.find("path");
    if (path == args.end() || !path->is_string() || path->get<std::string>().empty()) {
        result.is_error = true;
        result.payload =
            nlohmann::json{{"error",
                            "Argument \"path\" is required; call list_case_files for the paths this project "
                            "holds."}};
        return result;
    }
    if (!state_.current_project.has_value()) {
        result.is_error = true;
        result.payload =
            nlohmann::json{{"error", "This is a standalone SACM file, so there is nothing to switch between."}};
        return result;
    }

    const std::string wanted = path->get<std::string>();
    for (const core::ProjectFileEntry& entry : state_.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument || entry.relativePath.generic_string() != wanted) {
            continue;
        }
        if (!state_.open_project_file(entry)) {
            result.is_error = true;
            result.payload = nlohmann::json{
                {"error", state_.status_message.empty() ? ("Could not open " + wanted) : state_.status_message}};
            return result;
        }
        // Re-read after the switch so the caller sees the case it moved to.
        const agent::Result value = agent::GetCaseOverview(context);
        return OperationResult{value.payload, value.is_error, false};
    }

    result.is_error = true;
    result.payload = nlohmann::json{{"error", "No argument file in this project has the path \"" + wanted + "\"."}};
    return result;
}

Session::OperationResult Session::Run(const std::string& op, const nlohmann::json& args) {
    if (op == "get_connection_status") {
        // Probe first so the status is what the next tool call will actually
        // get, not what the last one got.
        EnsureConnected();
        return DescribeConnection();
    }

    EnsureConnected();

    if (connection_ != nullptr) {
        OperationResult result = RunOverBridge(op, args);
        if (!result.connection_lost) {
            return result;
        }

        // The application went away mid-call. Reads are answered by whatever
        // the application holds now, so retrying one after a successful
        // reconnect is safe and invisible. A mutation is never retried: the
        // application may have applied it before the connection broke, and a
        // second submission is exactly the kind of silent duplicate this
        // surface exists to prevent.
        if (EnsureConnected() && IsOfflineOperation(op)) {
            OperationResult retried = RunOverBridge(op, args);
            if (!retried.connection_lost) {
                return retried;
            }
        }

        // Probed here, not carried from before the retry: a retry that also
        // broke consumed the restored connection, and telling the client the
        // connection "has been restored" while it is down again would send the
        // next call into the same wall with the wrong expectation.
        const bool restored = EnsureConnected();
        const bool is_read = IsOfflineOperation(op);

        OperationResult failure;
        failure.is_error = true;
        failure.needs_application = true;
        if (is_read) {
            // A read has no application-side effect, so "applied" language
            // would be misleading; the only fact that matters is whether the
            // next attempt can succeed.
            failure.payload = nlohmann::json{
                {"error",
                 restored ? "The connection to Assurance Forge was interrupted while answering this read, and "
                            "again during the automatic retry. The connection has been restored -- retry the "
                            "call."
                          : "Assurance Forge is not reachable. It may have exited, or switched to a different "
                            "project. Start Assurance Forge and open this project; the session reconnects "
                            "automatically on the next call -- there is no need to restart this AI client."}};
        } else if (restored) {
            failure.payload = nlohmann::json{
                {"error",
                 "The connection to Assurance Forge was interrupted and has been restored. This operation was "
                 "not completed -- the application may or may not have applied it before the interruption. "
                 "Check the current state (get_draft_status or describe_working_draft) before retrying."}};
        } else {
            failure.payload = nlohmann::json{
                {"error",
                 "Assurance Forge is not reachable. It may have exited, or switched to a different project. "
                 "The interrupted operation may or may not have been applied before the connection broke -- "
                 "check get_draft_status once the application is back. Start Assurance Forge and open this "
                 "project; the session reconnects automatically on the next call -- there is no need to "
                 "restart this AI client."}};
        }
        return failure;
    }

    if (offline_loaded_) {
        if (denied_by_user_) {
            // The user declined this session's access while the application
            // was running. The copy on disk is the same safety case, and an
            // exit of the application must not turn a "no" into a "yes".
            OperationResult refused;
            refused.is_error = true;
            refused.payload =
                nlohmann::json{{"error",
                                "The user declined this session's access to the project, so its offline copy stays "
                                "closed too. Start Assurance Forge and grant access, or start a new session."},
                               {"error_code", bridge::error_code::kProjectAccessDenied}};
            return refused;
        }
        return RunOffline(op, args);
    }

    OperationResult result;
    result.is_error = true;
    result.needs_application = true;
    if (dynamic_) {
        result.payload = nlohmann::json{{"error", DynamicUnavailableDetail()}};
        return result;
    }
    result.payload = nlohmann::json{
        {"error",
         "Assurance Forge is not reachable, and this session holds no offline copy to read from. Start "
         "Assurance Forge and open this project; the session reconnects automatically on the next call."}};
    return result;
}

std::string Session::DynamicUnavailableDetail() const {
    if (last_discovery_count_ > 1) {
        return "More than one Assurance Forge is running, and this session never picks one by itself. "
               "Close the extra instances, or launch assurance-forge-mcp with --project <path> to name "
               "the project directly.";
    }
    if (last_discovery_count_ == 1) {
        // An instance was found but not reached -- a crashed process whose
        // record lingers, or a protocol mismatch between builds. "Start
        // Assurance Forge" would be the wrong instruction.
        return last_dynamic_error_.empty() ? "A running Assurance Forge was found but could not be reached. "
                                             "The session retries automatically on the next call."
                                           : last_dynamic_error_;
    }
    return "Start Assurance Forge and open a project. This session will discover it automatically.";
}

} // namespace mcp
