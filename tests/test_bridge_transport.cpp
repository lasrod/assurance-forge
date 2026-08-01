#include "bridge/transport.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace {

// A transport address nothing else on this machine is using. The two platforms
// name entirely different kinds of object, which is the whole reason the
// transport is behind an interface.
std::string UniqueAddress(const std::string& label) {
    const std::string unique = label + "-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                               std::to_string(reinterpret_cast<std::uintptr_t>(&label));
#ifdef _WIN32
    return "\\\\.\\pipe\\af-test-" + unique;
#else
    return (std::filesystem::temp_directory_path() / ("af-test-" + unique + ".sock")).string();
#endif
}

// Serves exactly one connection: reads messages and echoes each one back with a
// prefix, so the test can tell a real round trip from an accidental echo of its
// own buffer.
void ServeOneEchoConnection(bridge::Listener& listener, std::atomic<bool>& accepted) {
    std::string error;
    const std::unique_ptr<bridge::Connection> connection = listener.Accept(error);
    if (connection == nullptr) {
        return;
    }
    accepted.store(true);

    std::string message;
    while (connection->ReadMessage(message)) {
        if (!connection->WriteMessage("echo:" + message)) {
            return;
        }
    }
}

TEST(BridgeTransport, CarriesAMessageInBothDirections) {
    const std::string address = UniqueAddress("roundtrip");

    std::string error;
    const std::unique_ptr<bridge::Listener> listener = bridge::Listener::Start(address, error);
    ASSERT_NE(listener, nullptr) << error;

    std::atomic<bool> accepted{false};
    std::thread server([&] { ServeOneEchoConnection(*listener, accepted); });

    const std::unique_ptr<bridge::Connection> client = bridge::Connection::Connect(address, error);
    ASSERT_NE(client, nullptr) << error;

    ASSERT_TRUE(client->WriteMessage(R"({"op":"ping"})"));
    std::string reply;
    ASSERT_TRUE(client->ReadMessage(reply));
    EXPECT_EQ(reply, R"(echo:{"op":"ping"})");

    client->Close();
    listener->Stop();
    server.join();
    EXPECT_TRUE(accepted.load());
}

// Framing is newline-delimited over a byte stream, so a message larger than one
// read buffer must be reassembled rather than truncated. A `get_argument_tree`
// reply for a real case is comfortably past the chunk size.
TEST(BridgeTransport, ReassemblesAMessageLargerThanOneReadChunk) {
    const std::string address = UniqueAddress("large");

    std::string error;
    const std::unique_ptr<bridge::Listener> listener = bridge::Listener::Start(address, error);
    ASSERT_NE(listener, nullptr) << error;

    std::atomic<bool> accepted{false};
    std::thread server([&] { ServeOneEchoConnection(*listener, accepted); });

    const std::unique_ptr<bridge::Connection> client = bridge::Connection::Connect(address, error);
    ASSERT_NE(client, nullptr) << error;

    const std::string payload(200000, 'x');
    ASSERT_TRUE(client->WriteMessage(payload));

    std::string reply;
    ASSERT_TRUE(client->ReadMessage(reply));
    EXPECT_EQ(reply, "echo:" + payload);

    client->Close();
    listener->Stop();
    server.join();
}

// Two messages written back to back must arrive as two messages, not one
// concatenated blob and not a lost tail.
TEST(BridgeTransport, KeepsBackToBackMessagesSeparate) {
    const std::string address = UniqueAddress("framing");

    std::string error;
    const std::unique_ptr<bridge::Listener> listener = bridge::Listener::Start(address, error);
    ASSERT_NE(listener, nullptr) << error;

    std::atomic<bool> accepted{false};
    std::thread server([&] { ServeOneEchoConnection(*listener, accepted); });

    const std::unique_ptr<bridge::Connection> client = bridge::Connection::Connect(address, error);
    ASSERT_NE(client, nullptr) << error;

    ASSERT_TRUE(client->WriteMessage("first"));
    ASSERT_TRUE(client->WriteMessage("second"));

    std::string first;
    std::string second;
    ASSERT_TRUE(client->ReadMessage(first));
    ASSERT_TRUE(client->ReadMessage(second));
    EXPECT_EQ(first, "echo:first");
    EXPECT_EQ(second, "echo:second");

    client->Close();
    listener->Stop();
    server.join();
}

// The application's listener thread has to be unwound at exit. If `Stop` cannot
// wake a blocked `Accept`, closing the app hangs on a thread join.
TEST(BridgeTransport, StopUnblocksAWaitingAccept) {
    const std::string address = UniqueAddress("stop");

    std::string error;
    const std::unique_ptr<bridge::Listener> listener = bridge::Listener::Start(address, error);
    ASSERT_NE(listener, nullptr) << error;

    std::atomic<bool> returned{false};
    std::string accept_error = "not-yet-set";
    std::thread waiter([&] {
        const std::unique_ptr<bridge::Connection> connection = listener->Accept(accept_error);
        EXPECT_EQ(connection, nullptr);
        returned.store(true);
    });

    // Give Accept time to reach its wait; the test is still correct if it has
    // not, because Stop sets a flag Accept checks before waiting.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    listener->Stop();
    waiter.join();

    EXPECT_TRUE(returned.load());
    // A clean stop is reported as no error, so the caller can distinguish "we
    // are shutting down" from "the pipe broke".
    EXPECT_TRUE(accept_error.empty()) << accept_error;
}

// The normal case for the MCP adapter is that Assurance Forge is not running.
// That must fail promptly and say so, not hang waiting for a peer.
TEST(BridgeTransport, ConnectingToNothingFailsWithAMessage) {
    std::string error;
    const std::unique_ptr<bridge::Connection> connection = bridge::Connection::Connect(UniqueAddress("absent"), error);

    EXPECT_EQ(connection, nullptr);
    EXPECT_FALSE(error.empty());
}

// A peer that goes away mid-conversation must surface as a read failure, not as
// an empty message the caller might mistake for valid content.
TEST(BridgeTransport, ReportsAClosedPeerAsAReadFailure) {
    const std::string address = UniqueAddress("hangup");

    std::string error;
    const std::unique_ptr<bridge::Listener> listener = bridge::Listener::Start(address, error);
    ASSERT_NE(listener, nullptr) << error;

    std::thread server([&] {
        std::string accept_error;
        std::unique_ptr<bridge::Connection> connection = listener->Accept(accept_error);
        if (connection != nullptr) {
            connection->Close();
        }
    });

    const std::unique_ptr<bridge::Connection> client = bridge::Connection::Connect(address, error);
    ASSERT_NE(client, nullptr) << error;

    std::string message = "untouched";
    EXPECT_FALSE(client->ReadMessage(message));
    EXPECT_TRUE(message.empty());

    listener->Stop();
    server.join();
}

} // namespace
