#include "app/controllers/agent_bridge_controller.h"

#include "bridge/endpoint.h"
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

// The online path end to end: a client finds the endpoint record, connects over
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
        root_ = ShortTempRoot(
            "af-bridge-controller-" +
            std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);

        // Keeps the endpoint record out of the developer's real runtime
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
    static void DriveFrames(app::controllers::AgentBridgeController& controller,
                            std::atomic<bool>&                       stop) {
        while (!stop.load()) {
            controller.PollPendingRequests(
                [](const bridge::Request&                         request,
                   const app::controllers::AgentConnection& connection) {
                    return bridge::MakeResult(request.id,
                                              nlohmann::json{{"ranOperation", request.op},
                                                             {"client", connection.client_label}});
                });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    std::unique_ptr<bridge::Connection> ConnectToController(std::string& token) {
        bridge::EndpointRecord record;
        std::string            error;
        if (!bridge::ReadEndpointRecord(project_, record, error)) {
            ADD_FAILURE() << "no endpoint record was published: " << error;
            return nullptr;
        }
        token = record.token;
        std::unique_ptr<bridge::Connection> connection =
            bridge::Connection::Connect(record.address, error);
        if (connection == nullptr) {
            ADD_FAILURE() << "could not connect: " << error;
        }
        return connection;
    }

    static bridge::Response Exchange(bridge::Connection& connection,
                                     const bridge::Request& request) {
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

    static bridge::Request Say(const std::string& op, const std::string& token,
                               std::uint64_t id = 1) {
        bridge::Request request;
        request.id    = id;
        request.op    = op;
        request.token = token;
        return request;
    }

    std::filesystem::path root_;
    std::filesystem::path project_;

  private:
    void Remember(const std::string& name) {
        const char* value = std::getenv(name.c_str());
        saved_[name]      = value == nullptr ? std::string() : std::string(value);
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

TEST_F(AgentBridgeControllerTest, PublishesAnEndpointRecordAClientCanFind) {
    app::controllers::AgentBridgeController controller;
    std::string                             error;
    ASSERT_TRUE(controller.Start(project_, "0.1.0", error)) << error;
    EXPECT_TRUE(controller.listening());

    bridge::EndpointRecord record;
    ASSERT_TRUE(bridge::ReadEndpointRecord(project_, record, error)) << error;
    EXPECT_EQ(record.protocol, bridge::kProtocolVersion);
    EXPECT_EQ(record.app_version, "0.1.0");
    EXPECT_FALSE(record.token.empty());
    EXPECT_GT(record.pid, 0);
}

// A record outliving its process sends the next adapter to a pipe nobody is
// listening on, which presents as a hang rather than as "no app running".
TEST_F(AgentBridgeControllerTest, RemovesTheEndpointRecordOnStop) {
    app::controllers::AgentBridgeController controller;
    std::string                             error;
    ASSERT_TRUE(controller.Start(project_, "0.1.0", error)) << error;
    ASSERT_TRUE(std::filesystem::exists(bridge::EndpointRecordPath(project_)));

    controller.Stop();

    EXPECT_FALSE(controller.listening());
    EXPECT_FALSE(std::filesystem::exists(bridge::EndpointRecordPath(project_)));
}

TEST_F(AgentBridgeControllerTest, RunsAnOperationOnTheFrameThreadAndAnswers) {
    app::controllers::AgentBridgeController controller;
    std::string                             error;
    ASSERT_TRUE(controller.Start(project_, "0.1.0", error)) << error;

    std::atomic<bool> stop{false};
    std::thread       frames([&] { DriveFrames(controller, stop); });

    std::string                               token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    bridge::Request hello = Say(bridge::kHelloOperation, token, 1);
    hello.args            = nlohmann::json{{"client", "claude-ai 0.1.0"}};
    const bridge::Response greeting = Exchange(*client, hello);
    ASSERT_TRUE(greeting.ok) << greeting.error_message;
    EXPECT_EQ(greeting.result["appVersion"], "0.1.0");

    const bridge::Response answer = Exchange(*client, Say("get_case_overview", token, 2));
    ASSERT_TRUE(answer.ok) << answer.error_message;
    EXPECT_EQ(answer.result["ranOperation"], "get_case_overview");
    // Attribution survives the hop, so anything a connection produces can be
    // traced to the client that asked for it rather than to "the AI".
    EXPECT_EQ(answer.result["client"], "claude-ai 0.1.0");

    stop.store(true);
    frames.join();
}

TEST_F(AgentBridgeControllerTest, ShowsAConnectedClientToTheApplication) {
    app::controllers::AgentBridgeController controller;
    std::string                             error;
    ASSERT_TRUE(controller.Start(project_, "0.1.0", error)) << error;
    EXPECT_TRUE(controller.connections().empty());

    std::string                               token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    bridge::Request hello = Say(bridge::kHelloOperation, token, 1);
    hello.args            = nlohmann::json{{"client", "claude-ai 0.1.0"}};
    ASSERT_TRUE(Exchange(*client, hello).ok);

    const std::vector<app::controllers::AgentConnection> connected = controller.connections();
    ASSERT_EQ(connected.size(), 1u);
    EXPECT_EQ(connected[0].client_label, "claude-ai 0.1.0");
    EXPECT_FALSE(connected[0].connected_utc.empty());
}

// The token lives in the user's own runtime directory. A local process that did
// not read it is not the adapter this application published for.
TEST_F(AgentBridgeControllerTest, RefusesAConnectionWithTheWrongToken) {
    app::controllers::AgentBridgeController controller;
    std::string                             error;
    ASSERT_TRUE(controller.Start(project_, "0.1.0", error)) << error;

    std::string                               token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    const bridge::Response refused = Exchange(*client, Say(bridge::kHelloOperation, "not-the-token"));
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error_code, bridge::error_code::kUnauthorized);
}

TEST_F(AgentBridgeControllerTest, RefusesWorkBeforeTheHandshake) {
    app::controllers::AgentBridgeController controller;
    std::string                             error;
    ASSERT_TRUE(controller.Start(project_, "0.1.0", error)) << error;

    std::string                               token;
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
    std::string                             error;
    ASSERT_TRUE(controller.Start(project_, "0.1.0", error)) << error;

    std::string                               token;
    const std::unique_ptr<bridge::Connection> client = ConnectToController(token);
    ASSERT_NE(client, nullptr);

    bridge::Request stale = Say(bridge::kHelloOperation, token);
    stale.protocol        = bridge::kProtocolVersion + 41;

    const bridge::Response refused = Exchange(*client, stale);
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error_code, bridge::error_code::kUnsupportedProtocol);
    EXPECT_NE(refused.error_message.find(std::to_string(bridge::kProtocolVersion + 41)),
              std::string::npos);
}

// Switching projects must leave exactly one record. Two would send adapters to a
// listener that is no longer serving the project they asked for.
TEST_F(AgentBridgeControllerTest, MovesItsRecordWhenTheProjectChanges) {
    const std::filesystem::path second = root_ / "second-project";
    std::filesystem::create_directories(second);

    app::controllers::AgentBridgeController controller;
    std::string                             error;
    ASSERT_TRUE(controller.Start(project_, "0.1.0", error)) << error;
    ASSERT_TRUE(controller.Start(second, "0.1.0", error)) << error;

    EXPECT_FALSE(std::filesystem::exists(bridge::EndpointRecordPath(project_)));
    EXPECT_TRUE(std::filesystem::exists(bridge::EndpointRecordPath(second)));
    EXPECT_EQ(controller.project_root(), second);
}

} // namespace
