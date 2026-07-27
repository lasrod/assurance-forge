#include "mcp/session.h"

#include "agent/operations.h"
#include "bridge/endpoint.h"
#include "core/user_settings.h"

#include <nlohmann/json.hpp>

#include <cctype>

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

// Reads only. Anything that changes a safety case needs a command bus, an audit
// log and a human to accept it -- so it is available exactly where those are.
bool IsOfflineOperation(const std::string& op) {
    return op == "get_case_overview" || op == "find_elements" || op == "get_element" ||
           op == "get_argument_tree" || op == "list_case_files" || op == "open_case_file" ||
           op == "suggest_placement";
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

bool Session::ConnectToApplication(std::string& error) {
    bridge::EndpointRecord record;
    if (!bridge::ReadEndpointRecord(project_path_, record, error)) {
        return false;
    }
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
    hello.id    = next_request_id_++;
    hello.op    = bridge::kHelloOperation;
    hello.token = token_;
    hello.args  = nlohmann::json{{"client", client_label_.empty() ? "assurance-forge-mcp"
                                                                 : client_label_}};

    std::string reply;
    if (!connection_->WriteMessage(bridge::EncodeRequest(hello)) ||
        !connection_->ReadMessage(reply)) {
        error       = "Assurance Forge accepted the connection but did not complete a handshake.";
        connection_.reset();
        return false;
    }

    bridge::Response response;
    std::string      decode_error;
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
    const bool      is_directory = std::filesystem::is_directory(project_path_, ec);
    const bool      opened = (is_directory || !LooksLikeAssuranceCaseFile(project_path_))
                                 ? state_.open_project(project_path_.string())
                                 : state_.load_file(project_path_.string());
    if (!opened) {
        error = state_.status_message.empty() ? ("Could not open " + project_path_.string())
                                              : state_.status_message;
        return false;
    }

    if (state_.current_project.has_value() && !state_.loaded_case.has_value() &&
        !OpenProjectArgumentFile(state_)) {
        // Not fatal: the project may legitimately hold no argument yet, and a
        // session that can still describe the project is more useful than none.
        // The read tools report the absence rather than pretending to an empty
        // case.
        state_.load_warnings.push_back(
            "The project's assurance case could not be opened: " +
            (state_.status_message.empty() ? std::string("no SACM argument file")
                                           : state_.status_message));
    }
    return true;
}

std::unique_ptr<Session> Session::Open(Config config, std::string& error) {
    error.clear();

    if (config.project_path.empty()) {
        error = "No project path was given. Pass --project <path>.";
        return nullptr;
    }

    std::error_code ec;
    if (!std::filesystem::exists(config.project_path, ec)) {
        error = "Project path does not exist: " + config.project_path.string();
        return nullptr;
    }

    std::unique_ptr<Session> session(new Session());
    session->project_path_ = config.project_path;
    session->settings_path_ =
        config.settings_path.empty() ? core::UserSettingsFilePath() : config.settings_path;

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
    return session;
}

Session::OperationResult Session::RunOverBridge(const std::string&    op,
                                                const nlohmann::json& args) {
    OperationResult result;

    bridge::Request request;
    request.id    = next_request_id_++;
    request.op    = op;
    request.token = token_;
    request.args  = args;

    std::string reply;
    if (!connection_->WriteMessage(bridge::EncodeRequest(request)) ||
        !connection_->ReadMessage(reply)) {
        result.is_error = true;
        result.payload  = nlohmann::json{
            {"error", "Lost the connection to Assurance Forge. It may have closed the project or "
                      "exited. Reconnect this AI client to continue."}};
        return result;
    }

    bridge::Response response;
    std::string      decode_error;
    if (!bridge::DecodeResponse(reply, response, decode_error)) {
        result.is_error = true;
        result.payload  = nlohmann::json{{"error", "Assurance Forge sent a reply this version "
                                                   "could not read: " +
                                                       decode_error}};
        return result;
    }
    if (!response.ok) {
        result.is_error = true;
        result.payload  = nlohmann::json{{"error", response.error_message}};
        return result;
    }

    // The application marks a domain failure inside a successful response, the
    // same distinction MCP draws between a tool error and a protocol error.
    result.payload  = response.result;
    result.is_error = response.result.value("isError", false);
    result.payload.erase("isError");
    return result;
}

Session::OperationResult Session::RunOffline(const std::string& op, const nlohmann::json& args) {
    OperationResult result;

    if (!IsOfflineOperation(op)) {
        result.is_error          = true;
        result.needs_application = true;
        result.payload           = nlohmann::json{
            {"error",
                       "This needs Assurance Forge to be running with the project open. Changing a "
                       "safety case goes through the same audited, undoable path as an edit made in "
                       "the application, and a human accepts it there -- none of which exists in a "
                       "headless process. Open the project in Assurance Forge and reconnect."}};
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
        result.payload  = nlohmann::json{
            {"error",
              "Argument \"path\" is required; call list_case_files for the paths this project "
                       "holds."}};
        return result;
    }
    if (!state_.current_project.has_value()) {
        result.is_error = true;
        result.payload  = nlohmann::json{
            {"error", "This is a standalone SACM file, so there is nothing to switch between."}};
        return result;
    }

    const std::string wanted = path->get<std::string>();
    for (const core::ProjectFileEntry& entry : state_.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument ||
            entry.relativePath.generic_string() != wanted) {
            continue;
        }
        if (!state_.open_project_file(entry)) {
            result.is_error = true;
            result.payload  = nlohmann::json{
                {"error", state_.status_message.empty() ? ("Could not open " + wanted)
                                                         : state_.status_message}};
            return result;
        }
        // Re-read after the switch so the caller sees the case it moved to.
        const agent::Result value = agent::GetCaseOverview(context);
        return OperationResult{value.payload, value.is_error, false};
    }

    result.is_error = true;
    result.payload  = nlohmann::json{
        {"error", "No argument file in this project has the path \"" + wanted + "\"."}};
    return result;
}

Session::OperationResult Session::Run(const std::string& op, const nlohmann::json& args) {
    return connected() ? RunOverBridge(op, args) : RunOffline(op, args);
}

} // namespace mcp
