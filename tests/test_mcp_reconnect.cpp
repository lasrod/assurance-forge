#include "mcp/server.h"

#include "mcp/session.h"
#include "mcp/tools.h"

#include "bridge/instance_registry.h"
#include "bridge/protocol.h"
#include "bridge/transport.h"
#include "core/app_state.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// The session lifecycle. MCP clients launch `assurance-forge-mcp` when the
// *client* starts, so the application being absent, appearing later, restarting
// or switching projects are ordinary events in a session's life. These tests
// pin the recovery behavior: an offline session promotes itself when the
// application appears, a lost connection heals on the next call, reads are
// retried transparently, and a mutation is never replayed -- the application
// may have applied it before the connection broke.

namespace {

constexpr const char* kFakeVersion = "fake-app-9.9";

struct TempDir {
    std::filesystem::path path;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path UniqueTempPath(const std::string& stem) {
    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_mcp_reconnect_" + stem + "_" + std::to_string(++counter));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path WriteSettings(const std::filesystem::path& directory, bool consent) {
    const std::filesystem::path path = directory / "settings.json";
    std::ofstream(path, std::ios::trunc) << (consent ? R"({"mcp":{"enabled":true}})" : R"({"mcp":{"enabled":false}})");
    return path;
}

// Enough of the application to answer the bridge: publishes an instance record,
// accepts connections, answers hello/identify, and answers every domain
// operation with a payload naming itself, so a test can prove where an answer
// came from. `Crash()` is a kill -9: the listener dies, the instance record
// stays behind, exactly what reconnect has to cope with.
class FakeApplication {
public:
    explicit FakeApplication(std::filesystem::path project_root) : project_root_(std::move(project_root)) {}

    ~FakeApplication() {
        Crash();
        bridge::RemoveInstanceRecord(instance_id_);
    }

    bool Start() {
        token_ = bridge::GenerateToken();
        // A restart is a new process with a new instance id; the crashed
        // record disappears with it. In production pruning does this by pid
        // liveness, but every record here carries the test runner's own live
        // pid, so the fake sweeps its previous record explicitly.
        if (!instance_id_.empty()) {
            bridge::RemoveInstanceRecord(instance_id_);
        }
        instance_id_ = bridge::GenerateInstanceId();
        std::string error;
        listener_ = bridge::Listener::Start(bridge::InstanceAddress(instance_id_), error);
        if (listener_ == nullptr) {
            ADD_FAILURE() << "fake application could not listen: " << error;
            return false;
        }

        bridge::InstanceRecord record;
        record.protocol = bridge::kProtocolVersion;
        record.instance_id = instance_id_;
        // The test's own pid: discovery filters on liveness, and a record with
        // an invented pid would be pruned as a crashed instance's leftover.
        record.pid = OwnPid();
        record.address = listener_->address();
        record.token = token_;
        record.app_version = kFakeVersion;
        record.state = bridge::instance_state::kProjectOpen;
        record.project_key = bridge::ProjectKey(project_root_);
        record.last_heartbeat_utc = "2026-08-15T00:00:00Z";
        if (!bridge::WriteInstanceRecord(record, error)) {
            ADD_FAILURE() << "fake application could not publish its instance record: " << error;
            return false;
        }

        running_ = true;
        thread_ = std::thread([this] { Serve(); });
        return true;
    }

    static long long OwnPid() {
#ifdef _WIN32
        return static_cast<long long>(_getpid());
#else
        return static_cast<long long>(::getpid());
#endif
    }

    void Crash() {
        running_ = false;
        if (listener_ != nullptr) {
            listener_->Stop();
        }
        {
            const std::lock_guard<std::mutex> lock(connection_mutex_);
            if (connection_ != nullptr) {
                connection_->Close();
            }
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        listener_.reset();
    }

    // Close the connection when the next `count` domain operations arrive,
    // before answering them. From the adapter's side each is an interruption
    // mid-call; two in a row defeats the automatic read retry.
    void DropNextOperation(int count = 1) {
        drops_remaining_ = count;
    }

    int hello_count() const {
        return hello_count_.load();
    }
    int operation_count() const {
        return operation_count_.load();
    }

private:
    void Serve() {
        while (running_) {
            std::string error;
            std::unique_ptr<bridge::Connection> connection = listener_->Accept(error);
            if (connection == nullptr) {
                return;
            }
            {
                const std::lock_guard<std::mutex> lock(connection_mutex_);
                connection_ = connection.get();
            }
            ServeConnection(*connection);
            {
                const std::lock_guard<std::mutex> lock(connection_mutex_);
                connection_ = nullptr;
            }
        }
    }

    void ServeConnection(bridge::Connection& connection) {
        std::string line;
        while (connection.ReadMessage(line)) {
            bridge::Request request;
            std::string error;
            if (!bridge::DecodeRequest(line, request, error)) {
                return;
            }
            if (request.token != token_) {
                connection.WriteMessage(bridge::EncodeResponse(
                    bridge::MakeError(request.id, bridge::error_code::kUnauthorized, "bad token")));
                continue;
            }
            if (request.op == bridge::kHelloOperation) {
                ++hello_count_;
                connection.WriteMessage(bridge::EncodeResponse(
                    bridge::MakeResult(request.id, nlohmann::json{{"appVersion", kFakeVersion}})));
                continue;
            }
            if (request.op == bridge::kIdentifyOperation) {
                connection.WriteMessage(
                    bridge::EncodeResponse(bridge::MakeResult(request.id, nlohmann::json::object())));
                continue;
            }
            if (drops_remaining_.load() > 0) {
                --drops_remaining_;
                connection.Close();
                return;
            }
            ++operation_count_;
            connection.WriteMessage(bridge::EncodeResponse(bridge::MakeResult(
                request.id, nlohmann::json{{"answered_by", "fake-application"}, {"op", request.op}})));
        }
    }

    std::filesystem::path project_root_;
    std::string token_;
    std::string instance_id_;
    std::unique_ptr<bridge::Listener> listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> drops_remaining_{0};
    std::atomic<int> hello_count_{0};
    std::atomic<int> operation_count_{0};
    std::mutex connection_mutex_;
    bridge::Connection* connection_ = nullptr;
};

struct Fixture {
    TempDir workspace;
    std::filesystem::path project_root;
};

std::unique_ptr<Fixture> MakeProject(const std::string& stem) {
    std::unique_ptr<Fixture> fixture(new Fixture{TempDir{UniqueTempPath(stem)}, {}});
    core::AppState builder;
    if (!builder.create_empty_project("Project", fixture->workspace.path.string())) {
        ADD_FAILURE() << "could not create project: " << builder.status_message;
        return nullptr;
    }
    fixture->project_root = builder.current_project->rootPath;
    return fixture;
}

std::unique_ptr<mcp::Session> OpenSession(const Fixture& fixture, bool consent = true, bool never_connect = false) {
    mcp::Session::Config config;
    config.project_path = fixture.project_root;
    config.settings_path = WriteSettings(fixture.workspace.path, consent);
    config.never_connect = never_connect;

    std::string error;
    std::unique_ptr<mcp::Session> session = mcp::Session::Open(std::move(config), error);
    if (session == nullptr) {
        ADD_FAILURE() << "could not open MCP session: " << error;
    }
    return session;
}

std::string ErrorText(const mcp::Session::OperationResult& result) {
    return result.payload.value("error", std::string());
}

} // namespace

// An MCP client launched at machine startup opens its session before the user
// has started Assurance Forge. When the application appears, the very next call
// must be answered by it -- not by the stale copy, and not after a client
// restart.
TEST(McpReconnect, OfflineSessionPromotesItselfWhenTheApplicationAppears) {
    std::unique_ptr<Fixture> fixture = MakeProject("promote");
    ASSERT_NE(fixture, nullptr);

    std::unique_ptr<mcp::Session> session = OpenSession(*fixture);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(session->mode(), mcp::Session::Mode::Offline);

    const mcp::Session::OperationResult offline = session->Run("get_case_overview", nlohmann::json::object());
    ASSERT_FALSE(offline.is_error) << offline.payload.dump();
    EXPECT_EQ(offline.payload.value("view", std::string()), "accepted");

    FakeApplication application(fixture->project_root);
    ASSERT_TRUE(application.Start());

    const mcp::Session::OperationResult promoted = session->Run("get_case_overview", nlohmann::json::object());
    ASSERT_FALSE(promoted.is_error) << promoted.payload.dump();
    EXPECT_EQ(promoted.payload.value("answered_by", std::string()), "fake-application");
    EXPECT_TRUE(session->connected());
    EXPECT_EQ(session->application_version(), kFakeVersion);
}

// The application exits (or switches projects, which tears down the listener
// the same way). The session must say what happened and what to do in terms the
// user can act on, and must heal by itself when the application returns --
// restarting the AI client was the old, wrong answer.
TEST(McpReconnect, LostConnectionReportsActionablyAndHealsWhenTheApplicationReturns) {
    std::unique_ptr<Fixture> fixture = MakeProject("heal");
    ASSERT_NE(fixture, nullptr);

    FakeApplication application(fixture->project_root);
    ASSERT_TRUE(application.Start());

    std::unique_ptr<mcp::Session> session = OpenSession(*fixture);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->connected());

    application.Crash();

    const mcp::Session::OperationResult lost = session->Run("get_case_overview", nlohmann::json::object());
    EXPECT_TRUE(lost.is_error);
    EXPECT_TRUE(lost.needs_application);
    EXPECT_NE(ErrorText(lost).find("reconnects automatically"), std::string::npos) << ErrorText(lost);
    EXPECT_NE(ErrorText(lost).find("no need to restart"), std::string::npos)
        << "the error must say the client does not need restarting: " << ErrorText(lost);

    // The application comes back -- new process, new token, same project.
    ASSERT_TRUE(application.Start());

    const mcp::Session::OperationResult healed = session->Run("get_case_overview", nlohmann::json::object());
    ASSERT_FALSE(healed.is_error) << healed.payload.dump();
    EXPECT_EQ(healed.payload.value("answered_by", std::string()), "fake-application");
    EXPECT_TRUE(session->connected());
}

// A read interrupted mid-call is answered by whatever the application holds
// now, so after a successful reconnect it is retried invisibly: one Run call,
// one good answer, two handshakes.
TEST(McpReconnect, ReadIsRetriedTransparentlyAfterAnInterruption) {
    std::unique_ptr<Fixture> fixture = MakeProject("retry_read");
    ASSERT_NE(fixture, nullptr);

    FakeApplication application(fixture->project_root);
    ASSERT_TRUE(application.Start());

    std::unique_ptr<mcp::Session> session = OpenSession(*fixture);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->connected());

    application.DropNextOperation();

    const mcp::Session::OperationResult result = session->Run("get_case_overview", nlohmann::json::object());
    ASSERT_FALSE(result.is_error) << result.payload.dump();
    EXPECT_EQ(result.payload.value("answered_by", std::string()), "fake-application");
    EXPECT_EQ(application.hello_count(), 2) << "the retry must reconnect, not reuse the dead connection";
    EXPECT_EQ(application.operation_count(), 1);
}

// A read interrupted twice -- the original call and its automatic retry --
// reports connectivity, not application: a read has no application-side
// effect, so "may have been applied" language on this path would send the
// agent chasing a phantom draft change.
TEST(McpReconnect, ReadInterruptedTwiceReportsWithoutClaimingApplication) {
    std::unique_ptr<Fixture> fixture = MakeProject("retry_read_twice");
    ASSERT_NE(fixture, nullptr);

    FakeApplication application(fixture->project_root);
    ASSERT_TRUE(application.Start());

    std::unique_ptr<mcp::Session> session = OpenSession(*fixture);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->connected());

    application.DropNextOperation(2);

    const mcp::Session::OperationResult result = session->Run("get_case_overview", nlohmann::json::object());
    EXPECT_TRUE(result.is_error);
    EXPECT_NE(ErrorText(result).find("interrupted"), std::string::npos) << ErrorText(result);
    EXPECT_EQ(ErrorText(result).find("applied"), std::string::npos)
        << "a read error must not speak of the operation being applied: " << ErrorText(result);
    EXPECT_NE(ErrorText(result).find("restored"), std::string::npos)
        << "the error-path probe reconnected, and the message must say so: " << ErrorText(result);
    EXPECT_EQ(application.operation_count(), 0);

    const mcp::Session::OperationResult next = session->Run("get_case_overview", nlohmann::json::object());
    ASSERT_FALSE(next.is_error) << next.payload.dump();
    EXPECT_EQ(next.payload.value("answered_by", std::string()), "fake-application");
}

// A mutation interrupted mid-call may already have been applied -- the
// connection broke between the application acting and the response arriving.
// Replaying it could stage the same change twice, so the session reconnects but
// reports instead of retrying, and tells the agent how to find out what
// happened.
TEST(McpReconnect, MutationIsNeverRetriedAfterAnInterruption) {
    std::unique_ptr<Fixture> fixture = MakeProject("no_replay");
    ASSERT_NE(fixture, nullptr);

    FakeApplication application(fixture->project_root);
    ASSERT_TRUE(application.Start());

    std::unique_ptr<mcp::Session> session = OpenSession(*fixture);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->connected());

    application.DropNextOperation();

    const mcp::Session::OperationResult result = session->Run("begin_change_group", nlohmann::json::object());
    EXPECT_TRUE(result.is_error);
    EXPECT_NE(ErrorText(result).find("may or may not have applied"), std::string::npos) << ErrorText(result);
    EXPECT_NE(ErrorText(result).find("get_draft_status"), std::string::npos) << ErrorText(result);
    EXPECT_EQ(application.operation_count(), 0) << "the mutation was replayed";
    EXPECT_EQ(application.hello_count(), 2) << "the session should have reconnected before reporting";

    // The restored connection serves the next call normally.
    const mcp::Session::OperationResult next = session->Run("get_draft_status", nlohmann::json::object());
    ASSERT_FALSE(next.is_error) << next.payload.dump();
    EXPECT_EQ(next.payload.value("answered_by", std::string()), "fake-application");
}

// `never_connect` exists so tests stay deterministic on a developer machine
// that happens to have the project open. It must also suppress promotion, or
// every offline-mode test above this file becomes racy.
TEST(McpReconnect, NeverConnectSessionStaysOffline) {
    std::unique_ptr<Fixture> fixture = MakeProject("never");
    ASSERT_NE(fixture, nullptr);

    FakeApplication application(fixture->project_root);
    ASSERT_TRUE(application.Start());

    std::unique_ptr<mcp::Session> session = OpenSession(*fixture, /*consent=*/true, /*never_connect=*/true);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->mode(), mcp::Session::Mode::Offline);

    const mcp::Session::OperationResult result = session->Run("get_case_overview", nlohmann::json::object());
    ASSERT_FALSE(result.is_error) << result.payload.dump();
    EXPECT_EQ(result.payload.value("view", std::string()), "accepted") << "a never_connect session promoted itself";
    EXPECT_EQ(application.hello_count(), 0);
    EXPECT_FALSE(session->connected());
}

// The status tool is the debugging surface for everything above, so it must
// work in every mode and -- deliberately -- without consent: it returns no case
// content, and it is how the model learns which switch the user has to flip.
TEST(McpReconnect, ConnectionStatusToolReportsTheCurrentModeWithoutConsent) {
    std::unique_ptr<Fixture> fixture = MakeProject("status");
    ASSERT_NE(fixture, nullptr);

    std::unique_ptr<mcp::Session> session = OpenSession(*fixture, /*consent=*/false);
    ASSERT_NE(session, nullptr);

    const mcp::Session::OperationResult offline = session->Run("get_connection_status", nlohmann::json::object());
    ASSERT_FALSE(offline.is_error) << offline.payload.dump();
    EXPECT_EQ(offline.payload.value("mode", std::string()), "offline_read_only");
    EXPECT_FALSE(offline.payload.value("consent_granted", true));
    EXPECT_FALSE(offline.payload.value("consent_detail", std::string()).empty());

    FakeApplication application(fixture->project_root);
    ASSERT_TRUE(application.Start());

    const mcp::Session::OperationResult connected = session->Run("get_connection_status", nlohmann::json::object());
    ASSERT_FALSE(connected.is_error) << connected.payload.dump();
    EXPECT_EQ(connected.payload.value("mode", std::string()), "connected");
    EXPECT_EQ(connected.payload.value("application_version", std::string()), kFakeVersion);

    // And through the full server stack: the consent gate must not apply.
    mcp::Server server(*session);
    const std::optional<nlohmann::json> response = server.HandleMessage(nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params",
         {{"clientInfo", {{"name", "TestClient"}, {"version", "1.0"}}}}}}.dump());
    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->contains("result")) << response->dump();
    EXPECT_NE((*response)["result"].value("instructions", std::string()).find("Connected"), std::string::npos);

    const std::optional<nlohmann::json> call = server.HandleMessage(
        nlohmann::json{{"jsonrpc", "2.0"},
                       {"id", 2},
                       {"method", "tools/call"},
                       {"params", {{"name", "get_connection_status"}, {"arguments", nlohmann::json::object()}}}}
            .dump());
    ASSERT_TRUE(call.has_value());
    ASSERT_TRUE(call->contains("result")) << call->dump();
    EXPECT_FALSE((*call)["result"].value("isError", true))
        << "get_connection_status must not be behind the consent gate: " << call->dump();
}
