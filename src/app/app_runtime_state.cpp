#include "app/app_runtime_state.h"

#include "ai/libcurl_http_client.h"
#include "ai/openai_provider.h"
#include "core/commands/command_bus.h"
#include "sacm_adapter/case_projection.h"
#include "ui/imgui_buffer_utils.h"

namespace app {

AppRuntimeState::~AppRuntimeState() = default;

bool AppRuntimeState::IsProposalCanvasActive() const {
    return proposal_controller->IsCanvasActive();
}

void AppRuntimeState::RefreshDraftDocumentView() {
    if (!draft_document.active() || !app_state.loaded_case.has_value()) {
        // Guarded, because this is asked several times a frame and the no-draft
        // case is the ordinary one: clearing unconditionally would rebuild two
        // empty containers per call for every argument nobody is drafting
        // against.
        if (draft_document_view_revision == ~std::uint64_t{0})
            return;
        draft_document_view = parser::AssuranceCase{};
        draft_document_package = sacm::AssuranceCasePackage{};
        draft_document_changes = core::drafts::DraftDocumentDiff{};
        draft_document_view_differs = false;
        // Reset to the sentinel, not to the current revisions: leaving the
        // stamps behind would let a draft opened at the same revision as the one
        // just closed reuse this empty result.
        draft_document_view_revision = ~std::uint64_t{0};
        draft_document_view_case_revision = ~std::uint64_t{0};
        return;
    }
    const std::uint64_t revision = draft_document.revision();
    if (revision == draft_document_view_revision && app_state.case_revision == draft_document_view_case_revision)
        return;

    draft_document.ProjectViews(draft_document_view, draft_document_package);
    draft_document_changes = core::drafts::DiffAcceptedAgainstDraft(app_state.loaded_case.value(), draft_document_view);
    draft_document_view_differs = draft_document_changes.touches_anything();
    draft_document_view_revision = revision;
    draft_document_view_case_revision = app_state.case_revision;
}

const sacm::AssuranceCasePackage* AppRuntimeState::WorkingPackage() {
    if (DraftDocumentHasChanges())
        return &draft_document_package;
    return app_state.has_projected_package() ? &app_state.projected_package() : nullptr;
}

const core::drafts::DraftDocumentDiff& AppRuntimeState::DraftDocumentChanges() {
    RefreshDraftDocumentView();
    return draft_document_changes;
}

bool AppRuntimeState::DraftDocumentHasChanges() {
    RefreshDraftDocumentView();
    return draft_document_view_differs;
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
    agent_bridge = std::make_unique<controllers::AgentBridgeController>();
    ai.review_controller = std::make_unique<controllers::AiReviewController>(
        events, problems_manager, *review_controller, ai.task_runner, ai.service);
    LoadAiSettingsState();
    LoadMcpSettingsState();
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

void AppRuntimeState::LoadMcpSettingsState() {
    mcp_settings = core::LoadMcpUserSettings(ai.settings_store->SettingsPath());
}

void AppRuntimeState::RefreshStoredAiKeyState() {
    ai.key_stored = ai.service && ai.service->HasStoredApiKey();
}

} // namespace app
