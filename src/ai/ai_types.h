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

// The model a fresh install uses. A saved settings file keeps whatever the
// user chose; this is only the starting point.
constexpr const char* kDefaultOpenAiModel = "gpt-5.6-sol";

struct AiProviderSettings {
    AiProviderId provider = AiProviderId::OpenAI;
    std::string displayName = "OpenAI";
    std::string model = kDefaultOpenAiModel;
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

} // namespace ai
