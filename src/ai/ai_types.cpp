#include "ai/ai_types.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ai {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}  // namespace

const char* ToString(AiProviderId provider) {
    switch (provider) {
        case AiProviderId::OpenAI: return kOpenAiProviderName;
    }
    return kOpenAiProviderName;
}

const char* ToSettingsString(AiProviderId provider) {
    switch (provider) {
        case AiProviderId::OpenAI: return kOpenAiProviderId;
    }
    return kOpenAiProviderId;
}

AiProviderId AiProviderIdFromString(const std::string& value) {
    std::string normalized = Lower(value);
    if (normalized == kOpenAiProviderId || normalized == "openai" || normalized == "chatgpt") return AiProviderId::OpenAI;
    return AiProviderId::OpenAI;
}

const char* ToString(AiErrorCode errorCode) {
    switch (errorCode) {
        case AiErrorCode::None: return "None";
        case AiErrorCode::Disabled: return "AI support is disabled";
        case AiErrorCode::MissingApiKey: return "Missing API key";
        case AiErrorCode::SecureStoreUnavailable: return "Secure storage is unavailable";
        case AiErrorCode::AuthenticationFailed: return "Authentication failed";
        case AiErrorCode::NetworkError: return "Network error";
        case AiErrorCode::Timeout: return "Connection timed out";
        case AiErrorCode::RateLimited: return "Rate limit reached";
        case AiErrorCode::InvalidModel: return "Model not available";
        case AiErrorCode::MalformedResponse: return "Unexpected response";
        case AiErrorCode::ProviderError: return "AI provider error";
        case AiErrorCode::SettingsError: return "Settings error";
        case AiErrorCode::Unknown: return "Unknown error";
    }
    return "Unknown error";
}

AiConnectionStatus MakeStatus(AiTaskState state, AiErrorCode errorCode, std::string message) {
    AiConnectionStatus status;
    status.state = state;
    status.errorCode = errorCode;
    status.message = std::move(message);
    return status;
}

AiConnectionStatus SuccessStatus(std::string message) {
    return MakeStatus(AiTaskState::Success, AiErrorCode::None, std::move(message));
}

AiConnectionStatus ErrorStatus(AiErrorCode errorCode, std::string message) {
    return MakeStatus(AiTaskState::Error, errorCode, std::move(message));
}

}  // namespace ai
