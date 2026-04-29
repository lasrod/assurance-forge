#include "ai/openai_provider.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <utility>

namespace ai {
namespace {

AiErrorCode ErrorForHttpStatus(long status_code) {
    if (status_code == 401 || status_code == 403) return AiErrorCode::AuthenticationFailed;
    if (status_code == 404) return AiErrorCode::InvalidModel;
    if (status_code == 408 || status_code == 504) return AiErrorCode::Timeout;
    if (status_code == 429) return AiErrorCode::RateLimited;
    if (status_code >= 400) return AiErrorCode::ProviderError;
    return AiErrorCode::None;
}

std::string ExtractOutputText(const nlohmann::json& root) {
    if (root.contains("output_text") && root["output_text"].is_string()) {
        return root["output_text"].get<std::string>();
    }

    if (root.contains("output") && root["output"].is_array()) {
        std::ostringstream text;
        for (const auto& item : root["output"]) {
            if (!item.is_object() || !item.contains("content") || !item["content"].is_array()) continue;
            for (const auto& content : item["content"]) {
                if (!content.is_object()) continue;
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
nlohmann::json BuildRequestBody(const AiProviderSettings& settings, const AiRequest& request) {
    nlohmann::json body;
    body["model"] = settings.model.empty() ? kDefaultOpenAiModel : settings.model;
    body["input"] = request.userPrompt;
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

AiResponse ErrorResponse(AiErrorCode code, const std::string& message, std::string raw_json = {}, long http_status = 0) {
    AiResponse response;
    response.success = false;
    response.errorCode = code;
    response.errorMessage = message;
    response.rawJson = std::move(raw_json);
    response.httpStatus = http_status;
    return response;
}

std::string ErrorMessageWithDetail(const char* fallback, const std::string& detail) {
    if (detail.empty()) return fallback;
    return std::string(fallback) + ": " + RedactSensitiveText(detail);
}

}  // namespace

OpenAiProvider::OpenAiProvider(std::shared_ptr<IHttpClient> http_client)
    : http_client_(std::move(http_client)) {}

AiConnectionStatus OpenAiProvider::TestConnection(const AiProviderSettings& settings,
                                                  const std::string& api_key) {
    AiRequest request;
    request.userPrompt = kOpenAiConnectionTestPrompt;
    AiResponse response = Generate(settings, request, api_key);
    if (!response.success) {
        return ErrorStatus(response.errorCode, response.errorMessage.empty() ? ToString(response.errorCode) : response.errorMessage);
    }
    return SuccessStatus("Connection successful.");
}

AiResponse OpenAiProvider::Generate(const AiProviderSettings& settings,
                                    const AiRequest& request,
                                    const std::string& api_key) {
    if (!http_client_) {
        return ErrorResponse(AiErrorCode::NetworkError, "HTTP client is unavailable.");
    }
    if (api_key.empty()) {
        return ErrorResponse(AiErrorCode::MissingApiKey, "Missing API key.");
    }

    HttpRequest http_request;
    http_request.url = kOpenAiResponsesEndpoint;
    http_request.timeoutSeconds = 30;
    http_request.headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + api_key},
    };

    try {
        http_request.body = BuildRequestBody(settings, request).dump();
    } catch (const nlohmann::json::parse_error& e) {
        return ErrorResponse(AiErrorCode::SettingsError,
                             std::string("Invalid JSON schema: ") + e.what());
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
        AiErrorCode code = ErrorForHttpStatus(http_response.statusCode);
        return ErrorResponse(code, ToString(code), http_response.body, http_response.statusCode);
    }

    try {
        nlohmann::json root = nlohmann::json::parse(http_response.body);
        std::string text = ExtractOutputText(root);
        if (text.empty()) {
            return ErrorResponse(AiErrorCode::MalformedResponse, "Unexpected response.", http_response.body);
        }

        AiResponse response;
        response.success = true;
        response.errorCode = AiErrorCode::None;
        response.text = std::move(text);
        response.rawJson = http_response.body;
        response.httpStatus = http_response.statusCode;
        return response;
    } catch (...) {
        return ErrorResponse(AiErrorCode::MalformedResponse, "Unexpected response.", http_response.body);
    }
}

}  // namespace ai
