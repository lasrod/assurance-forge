#include "mcp/jsonrpc.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>

namespace {

TEST(McpJsonRpc, ParsesARequestWithParams) {
    const mcp::jsonrpc::ParseOutcome outcome = mcp::jsonrpc::ParseRequest(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"ping"}})");

    ASSERT_TRUE(outcome.request.has_value());
    EXPECT_FALSE(outcome.error_response.has_value());
    EXPECT_EQ(outcome.request->method, "tools/call");
    EXPECT_EQ(outcome.request->id, 7);
    EXPECT_FALSE(outcome.request->is_notification);
    EXPECT_EQ(outcome.request->params["name"], "ping");
}

// A request without an `id` is a notification, and JSON-RPC forbids answering
// one. Getting this wrong makes the server talk over its client.
TEST(McpJsonRpc, TreatsAMissingIdAsANotification) {
    const mcp::jsonrpc::ParseOutcome outcome =
        mcp::jsonrpc::ParseRequest(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");

    ASSERT_TRUE(outcome.request.has_value());
    EXPECT_TRUE(outcome.request->is_notification);
}

// An explicit null id is a badly formed *request*, not a notification, so it
// still deserves an error response.
TEST(McpJsonRpc, TreatsAnExplicitNullIdAsARequest) {
    const mcp::jsonrpc::ParseOutcome outcome =
        mcp::jsonrpc::ParseRequest(R"({"jsonrpc":"2.0","id":null,"method":"ping"})");

    ASSERT_TRUE(outcome.request.has_value());
    EXPECT_FALSE(outcome.request->is_notification);
}

TEST(McpJsonRpc, ReportsParseErrorForInvalidJson) {
    const mcp::jsonrpc::ParseOutcome outcome = mcp::jsonrpc::ParseRequest("{not json");

    EXPECT_FALSE(outcome.request.has_value());
    ASSERT_TRUE(outcome.error_response.has_value());
    EXPECT_EQ((*outcome.error_response)["error"]["code"], mcp::jsonrpc::kParseError);
}

TEST(McpJsonRpc, ReportsInvalidRequestWhenMethodIsMissing) {
    const mcp::jsonrpc::ParseOutcome outcome =
        mcp::jsonrpc::ParseRequest(R"({"jsonrpc":"2.0","id":1})");

    EXPECT_FALSE(outcome.request.has_value());
    ASSERT_TRUE(outcome.error_response.has_value());
    EXPECT_EQ((*outcome.error_response)["error"]["code"], mcp::jsonrpc::kInvalidRequest);
    // The id must survive so the client can correlate the failure to its call.
    EXPECT_EQ((*outcome.error_response)["id"], 1);
}

// stdout is the transport: a message spanning two lines would desynchronize the
// stream for the rest of the session, so serialization must stay compact.
TEST(McpJsonRpc, WritesExactlyOneLinePerMessage) {
    std::ostringstream out;
    mcp::jsonrpc::WriteMessage(out, nlohmann::json{{"a", 1}, {"nested", {{"b", 2}}}});

    const std::string written = out.str();
    EXPECT_EQ(std::count(written.begin(), written.end(), '\n'), 1);
    EXPECT_EQ(written.back(), '\n');
    EXPECT_EQ(written.find(' '), std::string::npos);
}

TEST(McpJsonRpc, ReadMessageSkipsBlankLinesAndStripsCarriageReturns) {
    std::istringstream in("\r\n   \n{\"method\":\"ping\"}\r\n");

    std::string message;
    ASSERT_TRUE(mcp::jsonrpc::ReadMessage(in, message));
    EXPECT_EQ(message, R"({"method":"ping"})");
    EXPECT_FALSE(mcp::jsonrpc::ReadMessage(in, message));
}

} // namespace
