#include "app/ai_error_text.h"

#include "ui/i18n/localization.h"

#include <string_view>

namespace app {
namespace {

// The message without one trailing full stop. The provider writes "Missing API
// key." where `ai::ToString` has "Missing API key"; both repeat the code.
std::string_view WithoutFinalStop(std::string_view text) {
    if (!text.empty() && text.back() == '.')
        text.remove_suffix(1);
    return text;
}

} // namespace

std::string LocalizedAiErrorCode(ai::AiErrorCode code) {
    // Each literal must equal the ai::ToString text for its code; a test holds
    // the two together, since the English here is the msgid the catalogue keys on.
    switch (code) {
    case ai::AiErrorCode::None:
        return AF_TR("None");
    case ai::AiErrorCode::Disabled:
        return AF_TR("AI support is disabled");
    case ai::AiErrorCode::MissingApiKey:
        return AF_TR("Missing API key");
    case ai::AiErrorCode::SecureStoreUnavailable:
        return AF_TR("Secure storage is unavailable");
    case ai::AiErrorCode::AuthenticationFailed:
        return AF_TR("Authentication failed");
    case ai::AiErrorCode::NetworkError:
        return AF_TR("Network error");
    case ai::AiErrorCode::Timeout:
        return AF_TR("Connection timed out");
    case ai::AiErrorCode::RateLimited:
        return AF_TR("Rate limit reached");
    case ai::AiErrorCode::QuotaExhausted:
        return AF_TR("The AI provider account has no credit left");
    case ai::AiErrorCode::InvalidModel:
        return AF_TR("Model not available");
    case ai::AiErrorCode::MalformedResponse:
        return AF_TR("Unexpected response");
    case ai::AiErrorCode::ProviderError:
        return AF_TR("AI provider error");
    case ai::AiErrorCode::SettingsError:
        return AF_TR("Settings error");
    case ai::AiErrorCode::Unknown:
        return AF_TR("Unknown error");
    }
    return AF_TR("Unknown error");
}

std::string LocalizedAiErrorMessage(ai::AiErrorCode code, const std::string& message) {
    const std::string localized_code = LocalizedAiErrorCode(code);
    if (message.empty() || WithoutFinalStop(message) == ai::ToString(code))
        return localized_code;
    return ui::i18n::trf("{0}: {1}", localized_code, message);
}

ai::AiConnectionStatus LocalizedAiStatus(ai::AiConnectionStatus status) {
    if (status.errorCode != ai::AiErrorCode::None) {
        status.message = LocalizedAiErrorMessage(status.errorCode, status.message);
        return status;
    }
    // The only texts the connection test reports without an error. Compared
    // against literals so the catalogue extractor sees each msgid.
    if (status.message == "Testing connection...")
        status.message = AF_TR("Testing connection...");
    else if (status.message == "Connection successful.")
        status.message = AF_TR("Connection successful.");
    return status;
}

} // namespace app
