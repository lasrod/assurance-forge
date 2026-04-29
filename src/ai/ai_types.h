#pragma once

#include <optional>
#include <string>

namespace ai {

enum class AiProviderId {
    OpenAI,
};

enum class AiErrorCode {
    None,
    Disabled,
    MissingApiKey,
    SecureStoreUnavailable,
    AuthenticationFailed,
    NetworkError,
    Timeout,
    RateLimited,
    InvalidModel,
    MalformedResponse,
    ProviderError,
    SettingsError,
    Unknown,
};

enum class AiTaskState {
    Idle,
    Running,
    Success,
    Error,
};

struct AiProviderSettings {
    AiProviderId provider = AiProviderId::OpenAI;
    std::string displayName = "OpenAI";
    std::string model = "gpt-5.5";
    bool enabled = false;
    bool sendProjectDataOnlyOnExplicitUserAction = true;
};

struct AiRequest {
    std::string systemInstruction;
    std::string userPrompt;
    std::optional<std::string> jsonSchemaName;
    std::optional<std::string> jsonSchema;
};

struct AiResponse {
    bool success = false;
    std::string text;
    std::string rawJson;
    std::string errorMessage;
    AiErrorCode errorCode = AiErrorCode::None;
    long httpStatus = 0;
};

struct AiConnectionStatus {
    AiTaskState state = AiTaskState::Idle;
    AiErrorCode errorCode = AiErrorCode::None;
    std::string message;
};

constexpr const char* kDefaultOpenAiModel = "gpt-5.5";
constexpr const char* kOpenAiProviderName = "OpenAI";
constexpr const char* kOpenAiProviderId = "openai";
constexpr const char* kOpenAiResponsesEndpoint = "https://api.openai.com/v1/responses";
constexpr const char* kSecretServiceName = "AssuranceForge";
constexpr const char* kOpenAiSecretAccount = "openai";
constexpr const char* kOpenAiConnectionTestPrompt = "Reply with exactly: Assurance Forge OpenAI connection works.";

const char* ToString(AiProviderId provider);
const char* ToSettingsString(AiProviderId provider);
AiProviderId AiProviderIdFromString(const std::string& value);
const char* ToString(AiErrorCode errorCode);

AiConnectionStatus MakeStatus(AiTaskState state, AiErrorCode errorCode, std::string message);
AiConnectionStatus SuccessStatus(std::string message);
AiConnectionStatus ErrorStatus(AiErrorCode errorCode, std::string message);

}  // namespace ai
