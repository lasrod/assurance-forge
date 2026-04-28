#include <gtest/gtest.h>

#include "ai/openai_provider.h"

#include <memory>

namespace {

class FakeHttpClient final : public ai::IHttpClient {
public:
    ai::HttpRequest lastRequest;
    ai::HttpResponse response;

    ai::HttpResponse Post(const ai::HttpRequest& request) override {
        lastRequest = request;
        return response;
    }
};

}  // namespace

TEST(OpenAiProviderTest, GeneratesTextFromOutputTextField) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.statusCode = 200;
    http->response.body = R"({"output_text":"hello"})";
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    settings.model = "gpt-test";
    ai::AiRequest request;
    request.userPrompt = "Say hello";

    ai::AiResponse response = provider.Generate(settings, request, "sk-test");

    ASSERT_TRUE(response.success) << response.errorMessage;
    EXPECT_EQ(response.text, "hello");
    EXPECT_NE(http->lastRequest.body.find("gpt-test"), std::string::npos);
    EXPECT_NE(http->lastRequest.body.find("Say hello"), std::string::npos);
}

TEST(OpenAiProviderTest, MapsAuthenticationFailureToSafeMessage) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.statusCode = 401;
    http->response.body = R"({"error":{"message":"bad key"}})";
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    ai::AiRequest request;
    request.userPrompt = "test";

    ai::AiResponse response = provider.Generate(settings, request, "sk-secret-value");

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.errorCode, ai::AiErrorCode::AuthenticationFailed);
    EXPECT_EQ(response.errorMessage, "Authentication failed");
    EXPECT_EQ(response.errorMessage.find("sk-secret-value"), std::string::npos);
}

TEST(OpenAiProviderTest, NetworkTimeoutMapsToTimeout) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.timedOut = true;
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    ai::AiRequest request;
    request.userPrompt = "test";

    ai::AiResponse response = provider.Generate(settings, request, "sk-test");

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.errorCode, ai::AiErrorCode::Timeout);
}

TEST(OpenAiProviderTest, NetworkErrorIncludesRedactedDiagnostic) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.networkError = true;
    http->response.errorMessage = "Could not resolve host: api.openai.com while using Bearer sk-secret-value";
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    ai::AiRequest request;
    request.userPrompt = "test";

    ai::AiResponse response = provider.Generate(settings, request, "sk-secret-value");

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.errorCode, ai::AiErrorCode::NetworkError);
    EXPECT_EQ(response.errorMessage,
              "Network error: Could not resolve host: api.openai.com while using Bearer [redacted]");
    EXPECT_EQ(response.errorMessage.find("sk-secret-value"), std::string::npos);
}

TEST(OpenAiProviderTest, MalformedSuccessResponseIsReported) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.statusCode = 200;
    http->response.body = R"({"id":"resp_1"})";
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    ai::AiRequest request;
    request.userPrompt = "test";

    ai::AiResponse response = provider.Generate(settings, request, "sk-test");

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.errorCode, ai::AiErrorCode::MalformedResponse);
}

TEST(OpenAiProviderTest, InvalidJsonSchemaReturnsSettingsError) {
    auto http = std::make_shared<FakeHttpClient>();
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    ai::AiRequest request;
    request.userPrompt = "test";
    request.jsonSchemaName = "my_schema";
    request.jsonSchema = "{ this is not valid json }";

    ai::AiResponse response = provider.Generate(settings, request, "sk-test");

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.errorCode, ai::AiErrorCode::SettingsError);
    EXPECT_NE(response.errorMessage.find("Invalid JSON schema"), std::string::npos);
}