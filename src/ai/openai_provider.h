#pragma once

#include "ai/ai_provider.h"
#include "ai/http_client.h"

#include <memory>

namespace ai {

class OpenAiProvider final : public IAiProvider {
public:
    explicit OpenAiProvider(std::shared_ptr<IHttpClient> http_client);

    AiProviderId ProviderId() const override { return AiProviderId::OpenAI; }
    AiConnectionStatus TestConnection(const AiProviderSettings& settings,
                                      const std::string& api_key) override;
    AiResponse Generate(const AiProviderSettings& settings,
                        const AiRequest& request,
                        const std::string& api_key) override;

private:
    std::shared_ptr<IHttpClient> http_client_;
};

}  // namespace ai
