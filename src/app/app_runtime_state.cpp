#include "app/app_runtime_state.h"

#include "ai/libcurl_http_client.h"
#include "ai/openai_provider.h"

#include <algorithm>
#include <cstring>

namespace app {
namespace {

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0) return;
    const size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

}  // namespace

bool AppRuntimeState::IsProposalCanvasActive() const {
    return proposal_controller->IsCanvasActive();
}

AppRuntimeState::AppRuntimeState()
    : ai_settings_store(std::make_shared<ai::AiSettingsStore>()),
      ai_secret_store(ai::CreatePlatformSecretStore()),
      ai_http_client(std::make_shared<ai::LibCurlHttpClient>()),
      ai_provider(std::make_shared<ai::OpenAiProvider>(ai_http_client)),
      ai_service(std::make_shared<ai::AiService>(ai_settings_store, ai_secret_store, ai_provider)) {
        element_edit_controller = std::make_unique<ElementEditController>(events);
        modal_coordinator = std::make_unique<ModalCoordinator>();
        project_controller = std::make_unique<ProjectController>();
        proposal_controller = std::make_unique<ProposalController>();
        review_controller = std::make_unique<ReviewController>(events);
        ai_review_controller = std::make_unique<AiReviewController>(events, problems_manager, ai_task_runner, ai_service);
    LoadAiSettingsState();
}

void AppRuntimeState::LoadAiSettingsState() {
    std::string warning;
    ai_settings = ai_service->LoadSettings(&warning);
    CopyToBuffer(ai_model_buf, sizeof(ai_model_buf), ai_settings.model);
    ai_secure_store_available = ai_secret_store && ai_secret_store->IsAvailable();
    RefreshStoredAiKeyState();
    if (!warning.empty()) {
        ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, warning);
    }
}

void AppRuntimeState::RefreshStoredAiKeyState() {
    ai_key_stored = ai_service && ai_service->HasStoredApiKey();
}

}  // namespace app
