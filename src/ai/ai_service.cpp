#include "ai/ai_service.h"

#include <utility>

namespace ai {

AiService::AiService(std::shared_ptr<AiSettingsStore> settings_store,
                     std::shared_ptr<ISecretStore> secret_store,
                     std::shared_ptr<IAiProvider> provider)
    : settings_store_(std::move(settings_store)),
      secret_store_(std::move(secret_store)),
      provider_(std::move(provider)) {}

AiProviderSettings AiService::LoadSettings(std::string* warning) const {
    return settings_store_ ? settings_store_->Load(warning) : AiProviderSettings{};
}

bool AiService::SaveSettings(const AiProviderSettings& settings, std::string& error) const {
    if (!settings_store_) {
        error = "AI settings store is unavailable.";
        return false;
    }
    return settings_store_->Save(settings, error);
}

bool AiService::IsEnabled() const {
    return LoadSettings().enabled;
}

bool AiService::HasConfiguredProvider() const {
    return provider_ != nullptr && LoadSettings().provider == provider_->ProviderId();
}

SecretLoadResult AiService::LoadApiKey() const {
    if (!secret_store_ || !secret_store_->IsAvailable()) {
        return SecretLoadFailure(AiErrorCode::SecureStoreUnavailable, "Secure storage is unavailable.");
    }
    return secret_store_->LoadSecret(kSecretServiceName, kOpenAiSecretAccount);
}

bool AiService::HasStoredApiKey() const {
    SecretLoadResult result = LoadApiKey();
    return result.success && result.secret.has_value() && !result.secret->empty();
}

SecretStoreResult AiService::SaveApiKey(const std::string& api_key) const {
    if (!secret_store_ || !secret_store_->IsAvailable()) {
        return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "Secure storage is unavailable.");
    }
    return secret_store_->SaveSecret(kSecretServiceName, kOpenAiSecretAccount, api_key);
}

SecretStoreResult AiService::DeleteApiKey() const {
    if (!secret_store_ || !secret_store_->IsAvailable()) {
        return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "Secure storage is unavailable.");
    }
    return secret_store_->DeleteSecret(kSecretServiceName, kOpenAiSecretAccount);
}

AiConnectionStatus AiService::TestConnection() const {
    AiProviderSettings settings = LoadSettings();
    if (!settings.enabled) {
        return ErrorStatus(AiErrorCode::Disabled, "AI support is disabled.");
    }
    if (!provider_ || provider_->ProviderId() != settings.provider) {
        return ErrorStatus(AiErrorCode::ProviderError, "AI provider is not configured.");
    }

    SecretLoadResult key = LoadApiKey();
    if (!key.success) return ErrorStatus(key.errorCode, key.errorMessage);
    if (!key.secret.has_value() || key.secret->empty()) {
        return ErrorStatus(AiErrorCode::MissingApiKey, "Missing API key.");
    }

    return provider_->TestConnection(settings, key.secret.value());
}

AiResponse AiService::Generate(const AiRequest& request) const {
    AiProviderSettings settings = LoadSettings();
    if (!settings.enabled) {
        AiResponse response;
        response.success = false;
        response.errorCode = AiErrorCode::Disabled;
        response.errorMessage = "AI support is disabled.";
        return response;
    }
    if (!provider_ || provider_->ProviderId() != settings.provider) {
        AiResponse response;
        response.success = false;
        response.errorCode = AiErrorCode::ProviderError;
        response.errorMessage = "AI provider is not configured.";
        return response;
    }

    SecretLoadResult key = LoadApiKey();
    if (!key.success || !key.secret.has_value() || key.secret->empty()) {
        AiResponse response;
        response.success = false;
        response.errorCode = key.success ? AiErrorCode::MissingApiKey : key.errorCode;
        response.errorMessage = key.success ? "Missing API key." : key.errorMessage;
        return response;
    }

    return provider_->Generate(settings, request, key.secret.value());
}

}  // namespace ai
