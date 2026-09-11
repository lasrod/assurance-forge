#pragma once

#include <optional>
#include <string>
#include <vector>

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
    // The account has no credit left. OpenAI reports it with the same HTTP 429
    // as a rate limit, and a user told "rate limit reached" waits and retries
    // when the only fix is topping up the account.
    QuotaExhausted,
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

    // Sampling controls, sent only when set.
    //
    // Empty means "whatever the provider defaults to", and that has to be
    // expressible: a reasoning model rejects `temperature` outright, so a
    // settings type that always sends a number cannot talk to one. Sending
    // nothing is also what the tool did before these existed, so an
    // unconfigured install behaves exactly as it did.
    //
    // They exist because a safety-case review should be as repeatable as the
    // provider allows. Two reviews of an unchanged argument that disagree are
    // not two opinions -- one of them is noise, and a reviewer has no way to
    // tell which.
    std::optional<double> temperature;
    std::optional<long long> seed;

    // The provider's processing tier, sent only when set. "flex" is OpenAI's
    // slower tier at batch prices: right for an evaluation sweep, wrong for a
    // user waiting on a review, so nothing in the application sets it.
    std::optional<std::string> serviceTier;
    // How long one request may take. A flex request can queue for minutes.
    int requestTimeoutSeconds = 120;
};

// One piece of a prompt. The pieces are sent in order and read as one text;
// `cacheBreakpoint` asks the provider to cache everything up to the end of
// this piece, so a later request starting with the same pieces reads them from
// the cache instead of paying for them again.
struct AiPromptSegment {
    std::string text;
    bool cacheBreakpoint = false;
};

struct AiRequest {
    std::string systemInstruction;
    // Used when `promptSegments` is empty.
    std::string userPrompt;
    // The prompt as cacheable pieces. When non-empty it replaces `userPrompt`.
    std::vector<AiPromptSegment> promptSegments;
    // Routes requests that share a cacheable prefix to the same cache. Omitted
    // when empty.
    std::string promptCacheKey;
    std::optional<std::string> jsonSchemaName;
    std::optional<std::string> jsonSchema;
};

// What a request consumed, as the provider reported it. `reported` is false
// when the response carried no usage block, so a zero is never mistaken for
// a free request.
struct AiUsage {
    bool reported = false;
    long long inputTokens = 0;
    // Of `inputTokens`: read from the cache, and written to it.
    long long cachedInputTokens = 0;
    long long cacheWriteTokens = 0;
    long long outputTokens = 0;
    // Of `outputTokens`: the model's hidden reasoning, billed as output.
    long long reasoningTokens = 0;
};

struct AiResponse {
    bool success = false;
    std::string text;
    std::string rawJson;
    std::string errorMessage;
    AiErrorCode errorCode = AiErrorCode::None;
    long httpStatus = 0;
    // The provider's own error code (e.g. "insufficient_quota"), when it sent one.
    std::string providerErrorCode;
    AiUsage usage;
    // The tier that actually served the request, when the provider says.
    std::string serviceTier;
};

// The prompt a request sends, whichever form it was given in.
std::string PromptText(const AiRequest& request);

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
