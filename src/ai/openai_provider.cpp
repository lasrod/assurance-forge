#include "ai/openai_provider.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

namespace ai {
namespace {

// The provider's own error code from an error body: `error.code`, or
// `error.type` when there is no code. Empty for anything else.
std::string ProviderErrorCode(const std::string& body) {
    const nlohmann::json root = nlohmann::json::parse(body, nullptr, false);
    if (!root.is_object() || !root.contains("error") || !root["error"].is_object())
        return {};
    const nlohmann::json& error = root["error"];
    for (const char* field : {"code", "type"}) {
        if (error.contains(field) && error[field].is_string() && !error[field].get<std::string>().empty())
            return error[field].get<std::string>();
    }
    return {};
}

bool IsQuotaExhausted(const std::string& provider_error_code) {
    return provider_error_code == "insufficient_quota" || provider_error_code == "credit_balance_exhausted";
}

AiErrorCode ErrorForHttpStatus(long status_code, const std::string& provider_error_code) {
    if (status_code == 401 || status_code == 403)
        return AiErrorCode::AuthenticationFailed;
    if (status_code == 404)
        return AiErrorCode::InvalidModel;
    if (status_code == 408 || status_code == 504)
        return AiErrorCode::Timeout;
    if (status_code == 429)
        return IsQuotaExhausted(provider_error_code) ? AiErrorCode::QuotaExhausted : AiErrorCode::RateLimited;
    if (status_code >= 400)
        return AiErrorCode::ProviderError;
    return AiErrorCode::None;
}

std::string ExtractOutputText(const nlohmann::json& root) {
    if (root.contains("output_text") && root["output_text"].is_string()) {
        return root["output_text"].get<std::string>();
    }

    if (root.contains("output") && root["output"].is_array()) {
        std::ostringstream text;
        for (const auto& item : root["output"]) {
            if (!item.is_object() || !item.contains("content") || !item["content"].is_array())
                continue;
            for (const auto& content : item["content"]) {
                if (!content.is_object())
                    continue;
                if (content.contains("text") && content["text"].is_string()) {
                    text << content["text"].get<std::string>();
                }
            }
        }
        return text.str();
    }

    return {};
}

// Builds the JSON request body. May throw nlohmann::json::parse_error if
// request.jsonSchema contains malformed JSON; callers should handle this.
// A segmented prompt is one user message whose text parts carry the cache
// breakpoints. Explicit mode caches only at the breakpoints the caller placed:
// the implicit breakpoint OpenAI would otherwise add at the end of the message
// writes the whole prompt to the cache at 1.25x the input price, and a review
// of an element nobody reviews again never reads it back.
nlohmann::json SegmentedInput(const AiRequest& request) {
    nlohmann::json content = nlohmann::json::array();
    for (const AiPromptSegment& segment : request.promptSegments) {
        nlohmann::json part = {{"type", "input_text"}, {"text", segment.text}};
        if (segment.cacheBreakpoint)
            part["prompt_cache_breakpoint"] = {{"mode", "explicit"}};
        content.push_back(std::move(part));
    }
    return nlohmann::json::array({{{"role", "user"}, {"content", std::move(content)}}});
}

bool HasCacheBreakpoint(const AiRequest& request) {
    for (const AiPromptSegment& segment : request.promptSegments) {
        if (segment.cacheBreakpoint)
            return true;
    }
    return false;
}

long long IntegerAt(const nlohmann::json& object, const char* field) {
    if (!object.is_object() || !object.contains(field) || !object[field].is_number_integer())
        return 0;
    return object[field].get<long long>();
}

// Reported only when both token counts are there as integers: a usage block
// without them measured nothing, and reading it as zeros would enter a billed
// request into a total as a free one.
AiUsage ParseUsage(const nlohmann::json& root) {
    AiUsage usage;
    if (!root.contains("usage") || !root["usage"].is_object())
        return usage;
    const nlohmann::json& reported = root["usage"];
    for (const char* field : {"input_tokens", "output_tokens"}) {
        if (!reported.contains(field) || !reported[field].is_number_integer())
            return usage;
    }
    usage.reported = true;
    usage.inputTokens = IntegerAt(reported, "input_tokens");
    usage.outputTokens = IntegerAt(reported, "output_tokens");
    if (reported.contains("input_tokens_details")) {
        usage.cachedInputTokens = IntegerAt(reported["input_tokens_details"], "cached_tokens");
        usage.cacheWriteTokens = IntegerAt(reported["input_tokens_details"], "cache_write_tokens");
    }
    if (reported.contains("output_tokens_details"))
        usage.reasoningTokens = IntegerAt(reported["output_tokens_details"], "reasoning_tokens");
    return usage;
}

nlohmann::json BuildRequestBody(const AiProviderSettings& settings, const AiRequest& request) {
    nlohmann::json body;
    body["model"] = settings.model.empty() ? kDefaultOpenAiModel : settings.model;
    if (request.promptSegments.empty())
        body["input"] = request.userPrompt;
    else
        body["input"] = SegmentedInput(request);
    // Explicit mode caches only at the breakpoints placed, so explicit mode with
    // none is how a request opts out: "when no explicit breakpoints are placed,
    // the request does not use prompt caching or create cache writes."
    if (HasCacheBreakpoint(request) || request.promptCacheDisabled)
        body["prompt_cache_options"] = {{"mode", "explicit"}};
    if (!request.promptCacheKey.empty())
        body["prompt_cache_key"] = request.promptCacheKey;
    if (settings.serviceTier.has_value())
        body["service_tier"] = settings.serviceTier.value();
    // Omitted rather than defaulted: a model that rejects the parameter must
    // still be reachable, and "the provider decides" is a real configuration
    // rather than a missing one.
    if (settings.temperature.has_value())
        body["temperature"] = settings.temperature.value();
    if (settings.seed.has_value())
        body["seed"] = settings.seed.value();
    if (!request.systemInstruction.empty()) {
        body["instructions"] = request.systemInstruction;
    }

    if (request.jsonSchemaName.has_value() && request.jsonSchema.has_value()) {
        body["text"]["format"] = {
            {"type", "json_schema"},
            {"name", request.jsonSchemaName.value()},
            {"schema", nlohmann::json::parse(request.jsonSchema.value())},
        };
    }

    return body;
}

AiResponse
ErrorResponse(AiErrorCode code, const std::string& message, std::string raw_json = {}, long http_status = 0) {
    AiResponse response;
    response.success = false;
    response.errorCode = code;
    response.errorMessage = message;
    response.rawJson = std::move(raw_json);
    response.httpStatus = http_status;
    return response;
}

std::string ErrorMessageWithDetail(const char* fallback, const std::string& detail) {
    if (detail.empty())
        return fallback;
    return std::string(fallback) + ": " + RedactSensitiveText(detail);
}

} // namespace

OpenAiProvider::OpenAiProvider(std::shared_ptr<IHttpClient> http_client) : http_client_(std::move(http_client)) {}

AiConnectionStatus OpenAiProvider::TestConnection(const AiProviderSettings& settings, const std::string& api_key) {
    AiRequest request;
    request.userPrompt = kOpenAiConnectionTestPrompt;
    AiResponse response = Generate(settings, request, api_key);
    if (!response.success) {
        return ErrorStatus(response.errorCode,
                           response.errorMessage.empty() ? ToString(response.errorCode) : response.errorMessage);
    }
    return SuccessStatus("Connection successful.");
}

AiResponse
OpenAiProvider::Generate(const AiProviderSettings& settings, const AiRequest& request, const std::string& api_key) {
    if (!http_client_) {
        return ErrorResponse(AiErrorCode::NetworkError, "HTTP client is unavailable.");
    }
    if (api_key.empty()) {
        return ErrorResponse(AiErrorCode::MissingApiKey, "Missing API key.");
    }

    HttpRequest http_request;
    http_request.url = kOpenAiResponsesEndpoint;
    http_request.timeoutSeconds = settings.requestTimeoutSeconds;
    http_request.headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + api_key},
    };

    try {
        http_request.body = BuildRequestBody(settings, request).dump();
    } catch (const nlohmann::json::parse_error& e) {
        return ErrorResponse(AiErrorCode::SettingsError, std::string("Invalid JSON schema: ") + e.what());
    }

    HttpResponse http_response = http_client_->Post(http_request);
    if (http_response.timedOut) {
        return ErrorResponse(AiErrorCode::Timeout,
                             ErrorMessageWithDetail("Connection timed out", http_response.errorMessage));
    }
    if (http_response.networkError) {
        return ErrorResponse(AiErrorCode::NetworkError,
                             ErrorMessageWithDetail("Network error", http_response.errorMessage));
    }

    if (http_response.statusCode < 200 || http_response.statusCode >= 300) {
        const std::string provider_error_code = ProviderErrorCode(http_response.body);
        AiErrorCode code = ErrorForHttpStatus(http_response.statusCode, provider_error_code);
        AiResponse response = ErrorResponse(code, ToString(code), http_response.body, http_response.statusCode);
        response.providerErrorCode = provider_error_code;
        return response;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(http_response.body);
        // Read before the output is judged: a response with no usable text was
        // still processed, and billed, and its usage belongs in whatever total
        // the caller keeps.
        const AiUsage usage = ParseUsage(root);
        std::string text = ExtractOutputText(root);
        if (text.empty()) {
            AiResponse response =
                ErrorResponse(AiErrorCode::MalformedResponse, "Unexpected response.", http_response.body);
            response.usage = usage;
            return response;
        }

        AiResponse response;
        response.success = true;
        response.errorCode = AiErrorCode::None;
        response.text = std::move(text);
        response.rawJson = http_response.body;
        response.httpStatus = http_response.statusCode;
        response.usage = usage;
        if (root.contains("service_tier") && root["service_tier"].is_string())
            response.serviceTier = root["service_tier"].get<std::string>();
        return response;
    } catch (...) {
        return ErrorResponse(AiErrorCode::MalformedResponse, "Unexpected response.", http_response.body);
    }
}

} // namespace ai
