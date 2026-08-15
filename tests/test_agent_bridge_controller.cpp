#include "app/controllers/agent_bridge_controller.h"

#include "bridge/instance_registry.h"
#include "bridge/protocol.h"
#include "bridge/transport.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>

// The online path end to end: a client finds the instance record, connects over
// a real pipe, shakes hands, and gets an answer produced on the thread that owns
// the model.
//
// The "frame thread" here is a loop calling PollPendingRequests, which is
// exactly what AppRuntime::RenderFrame does once per frame. Driving it from a
// test rather than from a window is the point: the design this replaces could
// only be exercised by launching the application, and its own pull request
// records that it was never actually observed working there.

namespace {

class AgentBridgeControllerTest : public ::testing::Test {
protected:
    // A POSIX socket path has to fit `sockaddr_un::sun_path` -- 108 bytes on
    // Linux, 104 on macOS -- and this root is the front of one, because the
    // fixture points the runtime directory at it.
    //
    // `temp_directory_path()` is not short enough to assume. On Linux it is
    // `/tmp` and the address came to 73 bytes; on a macOS runner it is
    // `/var/folders/<16>/<28>/T` and the same address came to 116, so `bind`
    // refused it and every test in this file failed there while Linux and
    // Windows stayed green. `/tmp` is required to exist by POSIX and is the
    // conventional home for sockets for exactly this reason.
    static std::filesystem::path ShortTempRoot(const std::string& name) {
#ifdef _WIN32
        return std::filesystem::temp_directory_path() / name;
#else
        return std::filesystem::path("/tmp") / name;
#endif
    }

    void SetUp() override {
        root_ = ShortTempRoot("af-bridge-controller-" +
                              std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);

        // Keeps the instance record out of the developer's real runtime
        // directory, where it would collide with an actually-running app.
        Remember("LOCALAPPDATA");
        Remember("XDG_RUNTIME_DIR");
        Remember("HOME");
        Set("LOCALAPPDATA", root_.string());
        Set("XDG_RUNTIME_DIR", root_.string());
        Set("HOME", root_.string());

        project_ = root_ / "project";
        std::filesystem::create_directories(project_);
    }

    void TearDown() override {
        for (const std::pair<const std::string, std::string>& entry : saved_) {
            Set(entry.first, entry.second);
        }
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    // Runs the controller's queue until `stop` is set, the way a frame loop
    // would. Every request is answered by echoing the operation name back, which
    // is enough to prove the round trip without dragging a model in.
    static void DriveFrames(app::controllers::AgentBridgeController& controller, std::atomic<bool>& stop) {
        while (!stop.load()) {
            controller.PollPendingRequests(
                [](const bridge::Request& request, const app::controllers::AgentConnection& connection) {
                    return bridge::MakeResult(request.id,
                                              nlohmann::json{{"ranOperation", request.op},
                                                             {"client", connection.client_label},
                                                             {"session", connection.session_id}});
                });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    std::unique_ptr<bridge::Connection> ConnectToController(std::string& token) {
        const std::vector<bridge::InstanceRecord> records = bridge::EnumerateInstanceRecords();
        if (records.size() != 1u) {
            ADD_FAILURE() << "expected exactly one instance record, found " << records.size();
            return nullptr;
        }
        token = records[0].token;
        std::string error;
        std::unique_ptr<bridge::Connection> connection = bridge::Connection::Connect(records[0].address, error);
        if (connection == nullptr) {
            ADD_FAILURE() << "could not connect: " << error;
        }
        return connection;
    }

    static bridge::Response Exchange(bridge::Connection& connection, const bridge::Request& request) {
        bridge::Response response;
        if (!connection.WriteMessage(bridge::EncodeRequest(request))) {
            ADD_FAILURE() << "could not send the request";
            return response;
        }
        std::string reply;
        if (!connection.ReadMessage(reply)) {
            ADD_FAILURE() << "no reply";
            return response;
        }
        std::string decode_error;
        bridge::DecodeResponse(reply, response, decode_error);
        return response;
    }

    static bridge::Request Say(const std::string& op, const std::string& token, std::uint64_t id = 1) {
        bridge::Request request;
        request.id = id;
        request.op = op;
        request.token = token;
        return request;
    }

    bridge::Request Hello(const std::string& token, std::uint64_t id, const std::string& session) const {
        bridge::Request request = Say(bridge::kHelloOperation, token, id);
        request.args = nlohmann::json{
            {"client", "assurance-forge-mcp"}, {"session", session}, {"projectKey", bridge::ProjectKey(project_)}};
        return request;
    }

    std::filesystem::path root_;
    std::filesystem::path project_;

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

    std::map<std::string, std::string> saved_;
};

TEST_F(AgentBridgeControllerTest, PublishesAnInstanceRecordAClientCanFind) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    EXPECT_TRUE(controller.listening());
    controller.SetActiveProject(project_);

    bridge::InstanceRecord record;
    ASSERT_TRUE(bridge::FindInstanceForProject(project_, record, error)) << error;
    EXPECT_EQ(record.protocol, bridge::kProtocolVersion);
    EXPECT_EQ(record.app_version, "0.1.0");
    EXPECT_EQ(record.instance_id, controller.instance_id());
    EXPECT_EQ(record.state, bridge::instance_state::kProjectOpen);
    EXPECT_FALSE(record.token.empty());
    EXPECT_GT(record.pid, 0);
}

// The listener exists independently of the current project (ADR 0014): it may
// run with no project open, publishing a record that says exactly that.
TEST_F(AgentBridgeControllerTest, ListensWithNoProjectOpen) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    EXPECT_TRUE(controller.listening());

    const std::vector<bridge::InstanceRecord> records = bridge::EnumerateInstanceRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].state, bridge::instance_state::kNoProject);
    EXPECT_TRUE(records[0].project_key.empty());

    // A session bound to a project gets a precise refusal, not content.
    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);
    const bridge::Response refused = Exchange(*client, Hello(token, 1, "stable-session-np"));
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error_code, bridge::error_code::kProjectNotActive);
}

// A record outliving its process sends the next adapter to a pipe nobody is
// listening on, which presents as a hang rather than as "no app running".
TEST_F(AgentBridgeControllerTest, RemovesTheInstanceRecordOnStop) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    ASSERT_EQ(bridge::EnumerateInstanceRecords().size(), 1u);

    controller.Stop();

    EXPECT_FALSE(controller.listening());
    EXPECT_TRUE(bridge::EnumerateInstanceRecords().empty());
}

TEST_F(AgentBridgeControllerTest, RunsAnOperationOnTheFrameThreadAndAnswers) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);

    std::atomic<bool> stop{false};
    std::thread frames([&] { DriveFrames(controller, stop); });

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    const bridge::Response greeting = Exchange(*client, Hello(token, 1, "stable-session-1"));
    ASSERT_TRUE(greeting.ok) << greeting.error_message;
    EXPECT_EQ(greeting.result["appVersion"], "0.1.0");

    bridge::Request identify = Say(bridge::kIdentifyOperation, token, 2);
    identify.args = nlohmann::json{{"client", "claude-ai 0.1.0"}};
    ASSERT_TRUE(Exchange(*client, identify).ok);

    const bridge::Response answer = Exchange(*client, Say("get_case_overview", token, 3));
    ASSERT_TRUE(answer.ok) << answer.error_message;
    EXPECT_EQ(answer.result["ranOperation"], "get_case_overview");
    // Attribution survives the hop, so anything a connection produces can be
    // traced to the client that asked for it rather than to "the AI".
    EXPECT_EQ(answer.result["client"], "claude-ai 0.1.0");
    EXPECT_EQ(answer.result["session"], "stable-session-1");

    stop.store(true);
    frames.join();
}

TEST_F(AgentBridgeControllerTest, ShowsAConnectedClientToTheApplication) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);
    EXPECT_TRUE(controller.connections().empty());

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    bridge::Request hello = Hello(token, 1, "stable-session-2");
    hello.args["client"] = "claude-ai 0.1.0";
    ASSERT_TRUE(Exchange(*client, hello).ok);

    const std::vector<app::controllers::AgentConnection> connected = controller.connections();
    ASSERT_EQ(connected.size(), 1u);
    EXPECT_EQ(connected[0].client_label, "claude-ai 0.1.0");
    EXPECT_EQ(connected[0].session_id, "stable-session-2");
    EXPECT_EQ(connected[0].project_key, bridge::ProjectKey(project_));
    EXPECT_FALSE(connected[0].connected_utc.empty());
}

TEST_F(AgentBridgeControllerTest, RefusesHelloUntilAStableSessionIdIsProvided) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    bridge::Request missing = Hello(token, 1, "unused");
    missing.args.erase("session");
    const bridge::Response missing_response = Exchange(*client, missing);
    EXPECT_FALSE(missing_response.ok);
    EXPECT_EQ(missing_response.error_code, bridge::error_code::kBadRequest);
    EXPECT_NE(missing_response.error_message.find("session"), std::string::npos);

    const bridge::Response empty_response = Exchange(*client, Hello(token, 2, ""));
    EXPECT_FALSE(empty_response.ok);
    EXPECT_EQ(empty_response.error_code, bridge::error_code::kBadRequest);

    EXPECT_TRUE(Exchange(*client, Hello(token, 3, "stable-session-3")).ok);
    ASSERT_EQ(controller.connections().size(), 1u);
    EXPECT_EQ(controller.connections()[0].session_id, "stable-session-3");
}

// A dynamic session's hello names no project. The connection is accepted --
// the session can report status -- but it is unbound: no project content, not
// even the project's name, until an access grant binds it (ADR 0014 gate 2).
TEST_F(AgentBridgeControllerTest, UnboundHelloConnectsButReceivesNoProjectContent) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);

    std::atomic<bool> stop{false};
    std::thread frames([&] { DriveFrames(controller, stop); });

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    bridge::Request hello = Hello(token, 1, "stable-session-4");
    hello.args.erase("projectKey");
    const bridge::Response greeting = Exchange(*client, hello);
    ASSERT_TRUE(greeting.ok) << greeting.error_message;
    // The coarse fact that a project is open is served; which project is not.
    EXPECT_EQ(greeting.result["projectOpen"], true);
    EXPECT_FALSE(greeting.result.contains("projectRoot"));

    const bridge::Response refused = Exchange(*client, Say("get_case_overview", token, 2));
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error_code, bridge::error_code::kProjectAccessRequired);

    stop.store(true);
    frames.join();
}

// Present-but-malformed is a client bug, not a request for an unbound
// connection: silently downgrading it would hide the bug and blur the
// handshake contract.
TEST_F(AgentBridgeControllerTest, RefusesAMalformedProjectKeyRatherThanDowngrading) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    bridge::Request empty_key = Hello(token, 1, "stable-session-6");
    empty_key.args["projectKey"] = "";
    const bridge::Response refused_empty = Exchange(*client, empty_key);
    EXPECT_FALSE(refused_empty.ok);
    EXPECT_EQ(refused_empty.error_code, bridge::error_code::kBadRequest);

    bridge::Request wrong_type = Hello(token, 2, "stable-session-6");
    wrong_type.args["projectKey"] = 42;
    const bridge::Response refused_type = Exchange(*client, wrong_type);
    EXPECT_FALSE(refused_type.ok);
    EXPECT_EQ(refused_type.error_code, bridge::error_code::kBadRequest);
}

// The token lives in the user's own runtime directory. A local process that did
// not read it is not the adapter this application published for.
TEST_F(AgentBridgeControllerTest, RefusesAConnectionWithTheWrongToken) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    const bridge::Response refused = Exchange(*client, Say(bridge::kHelloOperation, "not-the-token"));
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error_code, bridge::error_code::kUnauthorized);
}

TEST_F(AgentBridgeControllerTest, RefusesWorkBeforeTheHandshake) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    const bridge::Response refused = Exchange(*client, Say("get_case_overview", token));
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error_code, bridge::error_code::kNotInitialized);
}

// An old adapter meeting a new application is the expected failure once a client
// configuration has been sitting in a file for months. It has to be one clear,
// actionable message.
TEST_F(AgentBridgeControllerTest, RefusesAnUnsupportedProtocolByName) {
    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    bridge::Request stale = Say(bridge::kHelloOperation, token);
    stale.protocol = bridge::kProtocolVersion + 41;

    const bridge::Response refused = Exchange(*client, stale);
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error_code, bridge::error_code::kUnsupportedProtocol);
    EXPECT_NE(refused.error_message.find(std::to_string(bridge::kProtocolVersion + 41)), std::string::npos);
}

// The core of ADR 0014's no-silent-retargeting rule: a project switch does not
// tear the listener down, and it does not let a session bound to the old
// project read the new one. The same connection serves again, unchanged, when
// the user returns to the session's project.
TEST_F(AgentBridgeControllerTest, RefusesOperationsWhileTheBoundProjectIsInactive) {
    const std::filesystem::path second = root_ / "second-project";
    std::filesystem::create_directories(second);

    app::controllers::AgentBridgeController controller;
    std::string error;
    ASSERT_TRUE(controller.Start("0.1.0", error)) << error;
    controller.SetActiveProject(project_);
    const std::string instance_id = controller.instance_id();

    std::atomic<bool> stop{false};
    std::thread frames([&] { DriveFrames(controller, stop); });

    std::string token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(Exchange(*client, Hello(token, 1, "stable-session-5")).ok);
    ASSERT_TRUE(Exchange(*client, Say("get_case_overview", token, 2)).ok);

    // The user switches projects. One instance, one listener, one record --
    // whose fingerprint now names the other project.
    controller.SetActiveProject(second);
    EXPECT_TRUE(controller.listening());
    EXPECT_EQ(controller.instance_id(), instance_id);
    const std::vector<bridge::InstanceRecord> records = bridge::EnumerateInstanceRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].project_key, bridge::ProjectKey(second));

    // The bound session gets a refusal that names the situation, and no
    // content from the newly opened project.
    const bridge::Response refused = Exchange(*client, Say("get_case_overview", token, 3));
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error_code, bridge::error_code::kProjectNotActive);

    // Switching back restores service on the very same connection.
    controller.SetActiveProject(project_);
    const bridge::Response restored = Exchange(*client, Say("get_case_overview", token, 4));
    EXPECT_TRUE(restored.ok) << restored.error_message;

    stop.store(true);
    frames.join();
}

} // namespace
