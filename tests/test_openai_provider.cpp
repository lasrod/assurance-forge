#include "ai/openai_provider.h"

#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>

namespace {

class FakeHttpClient final : public ai::IHttpClient {
public:
    ai::HttpRequest lastRequest;
    ai::HttpResponse response;

    ai::HttpResponse Post(const ai::HttpRequest& request) override {
        lastRequest = request;
        return response;
    }

    ai::HttpResponse Get(const ai::HttpRequest& request) override {
        lastRequest = request;
        return response;
    }
};

} // namespace

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
    EXPECT_EQ(http->lastRequest.timeoutSeconds, 120);
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

// A segmented prompt is one user message whose parts carry the cache
// breakpoints, in explicit mode so the provider caches only where asked. The
// text the model reads is the same as when it was one string.
TEST(OpenAiProviderTest, SendsSegmentedPromptsWithExplicitCacheBreakpoints) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.statusCode = 200;
    http->response.body = R"({"output_text":"ok"})";
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    ai::AiRequest request;
    request.promptSegments = {{"shared ", true}, {"rules ", true}, {"element", false}};
    request.promptCacheKey = "sccg-0.9.0-claim_review-wording";

    ASSERT_TRUE(provider.Generate(settings, request, "sk-test").success);

    const nlohmann::json body = nlohmann::json::parse(http->lastRequest.body);
    EXPECT_EQ(body["prompt_cache_options"], nlohmann::json({{"mode", "explicit"}}));
    EXPECT_EQ(body["prompt_cache_key"], "sccg-0.9.0-claim_review-wording");
    ASSERT_TRUE(body["input"].is_array());
    ASSERT_EQ(body["input"].size(), 1u);
    EXPECT_EQ(body["input"][0]["role"], "user");
    const nlohmann::json& parts = body["input"][0]["content"];
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0]["type"], "input_text");
    EXPECT_EQ(parts[0]["text"], "shared ");
    EXPECT_EQ(parts[0]["prompt_cache_breakpoint"], nlohmann::json({{"mode", "explicit"}}));
    EXPECT_EQ(parts[1]["prompt_cache_breakpoint"], nlohmann::json({{"mode", "explicit"}}));
    EXPECT_FALSE(parts[2].contains("prompt_cache_breakpoint")) << "the element's data is not cached";
    EXPECT_EQ(ai::PromptText(request), "shared rules element");
}

// A plain prompt is sent exactly as before caching existed: no cache options,
// no key, no tier, the default timeout.
TEST(OpenAiProviderTest, SendsAPlainPromptWithNoCacheOrTierFields) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.statusCode = 200;
    http->response.body = R"({"output_text":"ok"})";
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    ai::AiRequest request;
    request.userPrompt = "plain";

    ASSERT_TRUE(provider.Generate(settings, request, "sk-test").success);

    const nlohmann::json body = nlohmann::json::parse(http->lastRequest.body);
    EXPECT_EQ(body["input"], "plain");
    for (const char* field : {"prompt_cache_options", "prompt_cache_key", "service_tier"})
        EXPECT_FALSE(body.contains(field)) << field;
    EXPECT_EQ(http->lastRequest.timeoutSeconds, 120);
}

TEST(OpenAiProviderTest, SendsTheServiceTierAndTimeoutTheSettingsName) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.statusCode = 200;
    http->response.body = R"({"output_text":"ok","service_tier":"flex"})";
    ai::OpenAiProvider provider(http);
    ai::AiProviderSettings settings;
    settings.serviceTier = "flex";
    settings.requestTimeoutSeconds = 900;
    ai::AiRequest request;
    request.userPrompt = "plain";

    ai::AiResponse response = provider.Generate(settings, request, "sk-test");

    ASSERT_TRUE(response.success);
    EXPECT_EQ(nlohmann::json::parse(http->lastRequest.body)["service_tier"], "flex");
    EXPECT_EQ(http->lastRequest.timeoutSeconds, 900);
    EXPECT_EQ(response.serviceTier, "flex") << "the tier that actually served it";
}

TEST(OpenAiProviderTest, ReportsTokenUsageIncludingCacheAndReasoning) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.statusCode = 200;
    http->response.body = R"({"output_text":"ok","usage":{"input_tokens":10507,
        "input_tokens_details":{"cached_tokens":8893,"cache_write_tokens":12},
        "output_tokens":2152,"output_tokens_details":{"reasoning_tokens":1513}}})";
    ai::OpenAiProvider provider(http);
    ai::AiRequest request;
    request.userPrompt = "plain";

    const ai::AiUsage usage = provider.Generate(ai::AiProviderSettings{}, request, "sk-test").usage;

    EXPECT_TRUE(usage.reported);
    EXPECT_EQ(usage.inputTokens, 10507);
    EXPECT_EQ(usage.cachedInputTokens, 8893);
    EXPECT_EQ(usage.cacheWriteTokens, 12);
    EXPECT_EQ(usage.outputTokens, 2152);
    EXPECT_EQ(usage.reasoningTokens, 1513);
}

TEST(OpenAiProviderTest, AResponseWithoutUsageSaysSoRatherThanReportingZero) {
    auto http = std::make_shared<FakeHttpClient>();
    http->response.statusCode = 200;
    http->response.body = R"({"output_text":"ok"})";
    ai::OpenAiProvider provider(http);
    ai::AiRequest request;
    request.userPrompt = "plain";

    EXPECT_FALSE(provider.Generate(ai::AiProviderSettings{}, request, "sk-test").usage.reported);
}

// OpenAI answers an account with no credit with the same HTTP 429 as a rate
// limit. Told "rate limit reached", a user waits and retries; the only fix is
// to top up the account.
TEST(OpenAiProviderTest, TellsAnExhaustedAccountFromARateLimit) {
    auto http = std::make_shared<FakeHttpClient>();
    ai::OpenAiProvider provider(http);
    ai::AiRequest request;
    request.userPrompt = "plain";

    http->response.statusCode = 429;
    http->response.body = R"({"error":{"message":"You have no credits remaining.","type":"insufficient_quota",
        "code":"credit_balance_exhausted"}})";
    ai::AiResponse exhausted = provider.Generate(ai::AiProviderSettings{}, request, "sk-test");
    EXPECT_EQ(exhausted.errorCode, ai::AiErrorCode::QuotaExhausted);
    EXPECT_EQ(exhausted.providerErrorCode, "credit_balance_exhausted");
    EXPECT_EQ(exhausted.errorMessage, "The AI provider account has no credit left");

    http->response.body =
        R"({"error":{"message":"Rate limit reached","type":"requests","code":"rate_limit_exceeded"}})";
    ai::AiResponse limited = provider.Generate(ai::AiProviderSettings{}, request, "sk-test");
    EXPECT_EQ(limited.errorCode, ai::AiErrorCode::RateLimited);
    EXPECT_EQ(limited.providerErrorCode, "rate_limit_exceeded");
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