#pragma once

#include "ai/ai_types.h"

#include <memory>
#include <optional>
#include <string>

namespace ai {

struct SecretStoreResult {
    bool success = false;
    AiErrorCode errorCode = AiErrorCode::None;
    std::string errorMessage;
};

struct SecretLoadResult {
    bool success = false;
    std::optional<std::string> secret;
    AiErrorCode errorCode = AiErrorCode::None;
    std::string errorMessage;
};

class ISecretStore {
public:
    virtual ~ISecretStore() = default;
    virtual bool IsAvailable() const = 0;
    virtual SecretStoreResult SaveSecret(const std::string& service,
                                         const std::string& account,
                                         const std::string& secret) = 0;
    virtual SecretLoadResult LoadSecret(const std::string& service,
                                        const std::string& account) = 0;
    virtual SecretStoreResult DeleteSecret(const std::string& service,
                                           const std::string& account) = 0;
};

SecretStoreResult SecretStoreSuccess();
SecretStoreResult SecretStoreFailure(AiErrorCode errorCode, std::string message);
SecretLoadResult SecretLoadSuccess(std::optional<std::string> secret);
SecretLoadResult SecretLoadFailure(AiErrorCode errorCode, std::string message);

std::shared_ptr<ISecretStore> CreatePlatformSecretStore();

}  // namespace ai
