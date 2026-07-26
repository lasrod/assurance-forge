#include "app/app_runtime_state.h"

#include "ai/libcurl_http_client.h"
#include "ai/openai_provider.h"
#include "core/commands/command_bus.h"
#include "ui/imgui_buffer_utils.h"

namespace app {

AppRuntimeState::~AppRuntimeState() = default;

bool AppRuntimeState::IsProposalCanvasActive() const {
    return proposal_controller->IsCanvasActive();
}

AppRuntimeState::AppRuntimeState() {
    ai.settings_store = std::make_shared<ai::AiSettingsStore>();
    ai.secret_store = ai::CreatePlatformSecretStore();
    ai.http_client = std::make_shared<ai::LibCurlHttpClient>();
    ai.provider = std::make_shared<ai::OpenAiProvider>(ai.http_client);
    ai.service = std::make_shared<ai::AiService>(ai.settings_store, ai.secret_store, ai.provider);

    element_edit_controller = std::make_unique<controllers::ElementEditController>(events);
    modal_coordinator = std::make_unique<controllers::ModalCoordinator>();
    project_controller = std::make_unique<controllers::ProjectController>();
    proposal_controller = std::make_unique<controllers::ProposalController>();
    review_controller = std::make_unique<controllers::ReviewController>(events);
    confidence_controller = std::make_unique<controllers::ConfidenceController>(events);
    register_controller = std::make_unique<controllers::RegisterController>(events);
    acp_controller = std::make_unique<controllers::AcpController>(events);
    ai.review_controller = std::make_unique<controllers::AiReviewController>(
        events, problems_manager, *review_controller, ai.task_runner, ai.service);
    LoadAiSettingsState();
}

void AppRuntimeState::LoadAiSettingsState() {
    std::string warning;
    ai.settings = ai.service->LoadSettings(&warning);
    ui::CopyToBuffer(ai.model_buf, sizeof(ai.model_buf), ai.settings.model);
    ai.secure_store_available = ai.secret_store && ai.secret_store->IsAvailable();
    RefreshStoredAiKeyState();
    if (!warning.empty()) {
        ai.connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, warning);
    }
}

void AppRuntimeState::RefreshStoredAiKeyState() {
    ai.key_stored = ai.service && ai.service->HasStoredApiKey();
}

} // namespace app
