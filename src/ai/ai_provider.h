#pragma once

#include "ai/ai_types.h"

#include <string>

namespace ai {

class IAiProvider {
public:
    virtual ~IAiProvider() = default;
    virtual AiProviderId ProviderId() const = 0;
    virtual AiConnectionStatus TestConnection(const AiProviderSettings& settings,
                                              const std::string& api_key) = 0;
    virtual AiResponse Generate(const AiProviderSettings& settings,
                                const AiRequest& request,
                                const std::string& api_key) = 0;
};

}  // namespace ai