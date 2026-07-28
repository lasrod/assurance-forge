#include "bridge/protocol.h"

#include <gtest/gtest.h>

#include <string>

namespace {

bridge::Request SampleRequest() {
    bridge::Request request;
    request.id    = 42;
    request.op    = "get_case_overview";
    request.token = "abcdef";
    request.args  = nlohmann::json{{"depth", 3}};
    return request;
}

TEST(BridgeProtocol, RoundTripsARequest) {
    bridge::Request decoded;
    std::string     error;
    ASSERT_TRUE(bridge::DecodeRequest(bridge::EncodeRequest(SampleRequest()), decoded, error))
        << error;

    EXPECT_EQ(decoded.protocol, bridge::kProtocolVersion);
    EXPECT_EQ(decoded.id, 42u);
    EXPECT_EQ(decoded.op, "get_case_overview");
    EXPECT_EQ(decoded.token, "abcdef");
    EXPECT_EQ(decoded.args["depth"], 3);
}

TEST(BridgeProtocol, RoundTripsASuccessfulResponse) {
    const bridge::Response encoded =
        bridge::MakeResult(42, nlohmann::json{{"elementCount", 108}});

    bridge::Response decoded;
    std::string      error;
    ASSERT_TRUE(bridge::DecodeResponse(bridge::EncodeResponse(encoded), decoded, error)) << error;

    EXPECT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.id, 42u);
    EXPECT_EQ(decoded.result["elementCount"], 108);
    EXPECT_TRUE(decoded.error_code.empty());
}

TEST(BridgeProtocol, RoundTripsAFailedResponse) {
    const bridge::Response encoded =
        bridge::MakeError(7, bridge::error_code::kUnauthorized, "Token does not match.");

    bridge::Response decoded;
    std::string      error;
    ASSERT_TRUE(bridge::DecodeResponse(bridge::EncodeResponse(encoded), decoded, error)) << error;

    EXPECT_FALSE(decoded.ok);
    EXPECT_EQ(decoded.error_code, bridge::error_code::kUnauthorized);
    EXPECT_EQ(decoded.error_message, "Token does not match.");
}

// The framing is newline-delimited, so a message that contains a newline would
// be split in transit and neither half would parse. Nothing this encoder emits
// may contain one.
TEST(BridgeProtocol, EncodesWithoutEmbeddedNewlines) {
    bridge::Request request = SampleRequest();
    request.args["summary"] = "first line\nsecond line";

    const std::string encoded = bridge::EncodeRequest(request);
    EXPECT_EQ(encoded.find('\n'), std::string::npos);

    bridge::Request decoded;
    std::string     error;
    ASSERT_TRUE(bridge::DecodeRequest(encoded, decoded, error)) << error;
    EXPECT_EQ(decoded.args["summary"], "first line\nsecond line");
}

TEST(BridgeProtocol, RejectsTextThatIsNotAJsonObject) {
    bridge::Request decoded;
    std::string     error;

    EXPECT_FALSE(bridge::DecodeRequest("not json at all", decoded, error));
    EXPECT_FALSE(error.empty());

    EXPECT_FALSE(bridge::DecodeRequest("[1,2,3]", decoded, error));
    EXPECT_FALSE(error.empty());
}

TEST(BridgeProtocol, RejectsARequestWithNoOperation) {
    bridge::Request decoded;
    std::string     error;
    EXPECT_FALSE(bridge::DecodeRequest(R"({"protocol":1,"id":1})", decoded, error));
    EXPECT_FALSE(error.empty());
}

// A failure carrying no code cannot be acted on, and treating it as a success
// would hand an empty result to something that edits a safety case.
TEST(BridgeProtocol, RejectsAFailureWithNoErrorCode) {
    bridge::Response decoded;
    std::string      error;
    EXPECT_FALSE(bridge::DecodeResponse(R"({"protocol":1,"id":1,"ok":false})", decoded, error));
    EXPECT_FALSE(error.empty());
}

// A message is read from a pipe, so every field is checked rather than trusted.
// A wrongly-typed field must not throw out of the decoder.
TEST(BridgeProtocol, SurvivesWronglyTypedFields) {
    bridge::Request decoded;
    std::string     error;
    ASSERT_TRUE(bridge::DecodeRequest(
        R"({"protocol":"one","id":"seven","op":"ping","token":5,"args":"nope"})", decoded, error))
        << error;

    EXPECT_EQ(decoded.protocol, 0);
    EXPECT_EQ(decoded.id, 0u);
    EXPECT_EQ(decoded.op, "ping");
    EXPECT_TRUE(decoded.token.empty());
    EXPECT_TRUE(decoded.args.is_object());
    EXPECT_TRUE(decoded.args.empty());
}

TEST(BridgeProtocol, AcceptsOnlyItsOwnProtocolVersion) {
    EXPECT_TRUE(bridge::IsSupportedProtocol(bridge::kProtocolVersion));
    EXPECT_FALSE(bridge::IsSupportedProtocol(bridge::kProtocolVersion + 1));
    EXPECT_FALSE(bridge::IsSupportedProtocol(0));
}

// The person who reads this text is looking at an AI client that stopped
// working. It has to name both versions and say what to do, or it is no better
// than silence.
TEST(BridgeProtocol, NamesBothVersionsWhenRefusingAMismatch) {
    const std::string message = bridge::UnsupportedProtocolMessage(99);

    EXPECT_NE(message.find(std::to_string(bridge::kProtocolVersion)), std::string::npos);
    EXPECT_NE(message.find("99"), std::string::npos);
    EXPECT_NE(message.find("assurance-forge-mcp"), std::string::npos);
}

} // namespace
