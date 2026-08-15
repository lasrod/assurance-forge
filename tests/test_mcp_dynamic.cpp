#include "mcp/session.h"

#include "app/controllers/agent_bridge_controller.h"
#include "bridge/instance_registry.h"
#include "bridge/protocol.h"
#include "core/app_state.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// Dynamic sessions (ADR 0014): `assurance-forge-mcp` launched with no project
// argument discovers the running application at call time and connects
// *unbound*. These tests drive the real controller and the real session
// together, so what they pin is the pair's behaviour, not either half's.
//
// The fail-closed interim matters most here: until access grants exist, an
// unbound session can report status and receives no project content -- the
// master consent flag alone must not disclose whatever project the user has
// open.

namespace {

long long OwnPid() {
#ifdef _WIN32
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(::getpid());
#endif
}

class McpDynamicTest : public ::testing::Test {
protected:
    // Short on purpose: the root fronts a POSIX socket path that must fit
    // sun_path (104 bytes on macOS). See test_agent_bridge_controller.cpp.
    static std::filesystem::path ShortTempRoot(const std::string& name) {
#ifdef _WIN32
        return std::filesystem::temp_directory_path() / name;
#else
        return std::filesystem::path("/tmp") / name;
#endif
    }

    void SetUp() override {
        root_ = ShortTempRoot("af-mcp-dynamic-" +
                              std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);

        // Dynamic discovery enumerates every instance record it can see, so
        // the runtime directory must be this test's own -- a developer's real
        // running Assurance Forge would otherwise be discovered too.
        Remember("LOCALAPPDATA");
        Remember("XDG_RUNTIME_DIR");
        Remember("HOME");
        Set("LOCALAPPDATA", root_.string());
        Set("XDG_RUNTIME_DIR", root_.string());
        Set("HOME", root_.string());

        settings_ = root_ / "settings.json";
        std::ofstream(settings_, std::ios::trunc) << R"({"mcp":{"enabled":true}})";
    }

    void TearDown() override {
        for (const std::pair<const std::string, std::string>& entry : saved_) {
            // A variable that was unset must be unset again, not set to "":
            // the two differ on POSIX and the difference leaks into every
            // later test in this process.
            if (entry.second.empty()) {
                Unset(entry.first);
            } else {
                Set(entry.first, entry.second);
            }
        }
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    std::unique_ptr<mcp::Session> OpenDynamicSession() {
        mcp::Session::Config config;
        config.settings_path = settings_;
        std::string error;
        std::unique_ptr<mcp::Session> session = mcp::Session::Open(std::move(config), error);
        if (session == nullptr) {
            ADD_FAILURE() << "a dynamic session must initialize with or without an application: " << error;
        }
        return session;
    }

    static void DriveFrames(app::controllers::AgentBridgeController& controller, std::atomic<bool>& stop) {
        while (!stop.load()) {
            controller.PollPendingRequests(
                [](const bridge::Request& request, const app::controllers::AgentConnection&) {
                    return bridge::MakeResult(request.id, nlohmann::json{{"ranOperation", request.op}});
                });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    static std::string ErrorText(const mcp::Session::OperationResult& result) {
        return result.payload.value("error", std::string());
    }

    std::filesystem::path root_;
    std::filesystem::path settings_;

private:
    void Remember(const std::string& name) {
        const char* value = std::getenv(name.c_str());
        saved_[name] = value == nullptr ? std::string() : std::string(value);
    }

    static void Set(const std::string& name, const std::string& value) {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    static void Unset(const std::string& name) {
#ifdef _WIN32
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }

    std::map<std::string, std::string> saved_;
};

// MCP clients launch the server at client startup, long before the user
// starts Assurance Forge. Initialization must succeed with nothing running,
// and the status must tell the client what will happen next.
TEST_F(McpDynamicTest, InitializesWithNoApplicationAndSaysSo) {
    std::unique_ptr<mcp::Session> session = OpenDynamicSession();
    ASSERT_NE(session, nullptr);

    const mcp::Session::OperationResult status = session->Run("get_connection_status", nlohmann::json::object());
    ASSERT_FALSE(status.is_error) << status.payload.dump();
    EXPECT_EQ(status.payload.value("mode", std::string()), "application_unavailable");
    EXPECT_NE(status.payload.value("detail", std::string()).find("discover it automatically"), std::string::npos);

    const mcp::Session::OperationResult read = session->Run("get_case_overview", nlohmann::json::object());
    EXPECT_TRUE(read.is_error);
    EXPECT_TRUE(read.needs_application);
    EXPECT_NE(ErrorText(read).find("discover it automatically"), std::string::npos) << ErrorText(read);
}

// The application appears after the session opened. The next call finds it --
// and connects unbound: status names the state, and project content is
// refused by the application, not served on the strength of the master flag.
TEST_F(McpDynamicTest, DiscoversTheApplicationAndStaysUnbound) {
    std::unique_ptr<mcp::Session> session = OpenDynamicSession();
    ASSERT_NE(session, nullptr);

    const std::filesystem::path project = root_ / "project";
    std::filesystem::create_directories(project);

    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.9-test", error)) << error;
    controller.SetActiveProject(project);

    std::atomic<bool> stop{false};
    std::thread frames([&] { DriveFrames(controller, stop); });

    const mcp::Session::OperationResult status = session->Run("get_connection_status", nlohmann::json::object());
    ASSERT_FALSE(status.is_error) << status.payload.dump();
    EXPECT_EQ(status.payload.value("mode", std::string()), "connected_unbound");
    EXPECT_EQ(status.payload.value("application_version", std::string()), "0.9-test");
    // The session's own configuration is empty; no project identity leaks in.
    EXPECT_EQ(status.payload.value("project_path", std::string()), "");

    // The first read raises the access request and is refused while pending.
    const mcp::Session::OperationResult refused = session->Run("get_case_overview", nlohmann::json::object());
    EXPECT_TRUE(refused.is_error);
    EXPECT_NE(ErrorText(refused).find("approve this session's access"), std::string::npos) << ErrorText(refused);

    const mcp::Session::OperationResult asked = session->Run("request_project_access", nlohmann::json::object());
    ASSERT_FALSE(asked.is_error) << asked.payload.dump();
    EXPECT_EQ(asked.payload.value("status", std::string()), "pending");

    // The user allows it, and the very same dynamic session reads.
    controller.GrantAccess(session->session_id());
    const mcp::Session::OperationResult granted = session->Run("get_case_overview", nlohmann::json::object());
    ASSERT_FALSE(granted.is_error) << granted.payload.dump();
    EXPECT_EQ(granted.payload.value("ranOperation", std::string()), "get_case_overview");

    stop.store(true);
    frames.join();
    controller.Stop();
}

// An instance was found but cannot be reached -- here, a future protocol.
// "Start Assurance Forge" would be the wrong instruction, so the status must
// surface the actual failure instead.
TEST_F(McpDynamicTest, SurfacesWhyAFoundInstanceCannotBeReached) {
    bridge::InstanceRecord future;
    future.protocol = bridge::kProtocolVersion + 41;
    future.instance_id = "af-futureprotocol";
    future.pid = OwnPid();
    future.address = bridge::InstanceAddress("af-futureprotocol");
    future.token = bridge::GenerateToken();
    future.app_version = "99.0";
    future.state = bridge::instance_state::kNoProject;
    future.last_heartbeat_utc = "2026-08-15T00:00:00Z";
    std::string error;
    ASSERT_TRUE(bridge::WriteInstanceRecord(future, error)) << error;

    std::unique_ptr<mcp::Session> session = OpenDynamicSession();
    ASSERT_NE(session, nullptr);

    const mcp::Session::OperationResult status = session->Run("get_connection_status", nlohmann::json::object());
    ASSERT_FALSE(status.is_error) << status.payload.dump();
    EXPECT_EQ(status.payload.value("mode", std::string()), "application_unreachable");
    EXPECT_NE(status.payload.value("detail", std::string()).find("protocol"), std::string::npos)
        << status.payload.value("detail", std::string());

    const mcp::Session::OperationResult read = session->Run("get_case_overview", nlohmann::json::object());
    EXPECT_TRUE(read.is_error);
    EXPECT_NE(ErrorText(read).find("protocol"), std::string::npos) << ErrorText(read);

    bridge::RemoveInstanceRecord("af-futureprotocol");
}

// Two live instances: the session must refuse to choose. Picking the newest
// is picking a safety case by timestamp.
TEST_F(McpDynamicTest, RefusesToChooseAmongMultipleInstances) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.9-test", error)) << error;

    // A second live instance, real enough for discovery: its pid is this test
    // process, which is alive.
    bridge::InstanceRecord other;
    other.protocol = bridge::kProtocolVersion;
    other.instance_id = "af-secondinstance";
    other.pid = OwnPid();
    other.address = bridge::InstanceAddress("af-secondinstance");
    other.token = bridge::GenerateToken();
    other.app_version = "0.9-test";
    other.state = bridge::instance_state::kNoProject;
    other.last_heartbeat_utc = "2026-08-15T00:00:00Z";
    ASSERT_TRUE(bridge::WriteInstanceRecord(other, error)) << error;

    std::unique_ptr<mcp::Session> session = OpenDynamicSession();
    ASSERT_NE(session, nullptr);

    const mcp::Session::OperationResult status = session->Run("get_connection_status", nlohmann::json::object());
    ASSERT_FALSE(status.is_error) << status.payload.dump();
    EXPECT_EQ(status.payload.value("mode", std::string()), "multiple_applications");

    const mcp::Session::OperationResult refused = session->Run("get_case_overview", nlohmann::json::object());
    EXPECT_TRUE(refused.is_error);
    EXPECT_NE(ErrorText(refused).find("never picks one"), std::string::npos) << ErrorText(refused);

    bridge::RemoveInstanceRecord("af-secondinstance");
    controller.Stop();
}

// --offline-project is the deliberate escape hatch: accepted SACM, read-only,
// and it never connects -- even while an application with the same project
// open is right there.
TEST_F(McpDynamicTest, ExplicitOfflineModeIsReadOnlyAndNeverConnects) {
    core::AppState builder;
    ASSERT_TRUE(builder.create_empty_project("Project", (root_ / "workspace").string())) << builder.status_message;
    const std::filesystem::path project_root = builder.current_project->rootPath;

    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.9-test", error)) << error;
    controller.SetActiveProject(project_root);

    mcp::Session::Config config;
    config.project_path = project_root;
    config.settings_path = settings_;
    config.offline_only = true;
    std::unique_ptr<mcp::Session> session = mcp::Session::Open(std::move(config), error);
    ASSERT_NE(session, nullptr) << error;

    const mcp::Session::OperationResult status = session->Run("get_connection_status", nlohmann::json::object());
    ASSERT_FALSE(status.is_error) << status.payload.dump();
    EXPECT_EQ(status.payload.value("mode", std::string()), "offline_read_only");
    EXPECT_NE(status.payload.value("detail", std::string()).find("never connects"), std::string::npos);

    const mcp::Session::OperationResult read = session->Run("get_case_overview", nlohmann::json::object());
    ASSERT_FALSE(read.is_error) << read.payload.dump();
    EXPECT_EQ(read.payload.value("view", std::string()), "accepted");

    const mcp::Session::OperationResult mutation = session->Run("begin_change_group", nlohmann::json::object());
    EXPECT_TRUE(mutation.is_error);
    EXPECT_NE(ErrorText(mutation).find("--offline-project"), std::string::npos) << ErrorText(mutation);

    controller.Stop();
}

TEST_F(McpDynamicTest, RefusesOfflineOnlyWithoutAPath) {
    mcp::Session::Config config;
    config.settings_path = settings_;
    config.offline_only = true;
    std::string error;
    EXPECT_EQ(mcp::Session::Open(std::move(config), error), nullptr);
    EXPECT_NE(error.find("--offline-project"), std::string::npos);
}

} // namespace
