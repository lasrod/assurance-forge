#pragma once

#include "ai/ai_provider.h"
#include "ai/ai_settings.h"
#include "ai/secret_store.h"

#include <memory>

namespace ai {

class AiService {
public:
    AiService(std::shared_ptr<AiSettingsStore> settings_store,
              std::shared_ptr<ISecretStore> secret_store,
              std::shared_ptr<IAiProvider> provider);

    AiProviderSettings LoadSettings(std::string* warning = nullptr) const;
    bool SaveSettings(const AiProviderSettings& settings, std::string& error) const;

    bool IsEnabled() const;
    bool HasConfiguredProvider() const;
    bool HasStoredApiKey() const;

    SecretStoreResult SaveApiKey(const std::string& api_key) const;
    SecretStoreResult DeleteApiKey() const;

    AiConnectionStatus TestConnection() const;
    AiResponse Generate(const AiRequest& request) const;

private:
    SecretLoadResult LoadApiKey() const;

    std::shared_ptr<AiSettingsStore> settings_store_;
    std::shared_ptr<ISecretStore> secret_store_;
    std::shared_ptr<IAiProvider> provider_;
};

}  // namespace ai
