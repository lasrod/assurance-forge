#include "app/app_runtime.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "app/native_file_dialogs.h"
#include "app/recent_projects.h"

#include "ai/ai_claim_review.h"
#include "ai/ai_service.h"
#include "ai/ai_task_runner.h"
#include "ai/libcurl_http_client.h"
#include "ai/openai_provider.h"
#include "ai/secret_store.h"
#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_theme.h"
#include "imgui.h"

#include "core/app_state.h"
#include "core/element_factory.h"
#include "core/problems/problems_manager.h"
#include "core/project_service.h"
#include "parser/guidelines_parser.h"
#include "ui/gsn/gsn_adapter.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/localization.h"
#include "ui/panels/element_panel.h"
#include "ui/panels/problems_panel.h"
#include "ui/panels/preferences_panel.h"
#include "ui/panels/project_files_panel.h"
#include "ui/panels/sacm_viewer_panel.h"
#include "ui/panels/welcome_modal.h"
#include "ui/register_views.h"
#include "ui/tree_view.h"
#include "ui/ui_state.h"
#include "ui/widgets/splitter.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace app {
namespace {

constexpr size_t kPathBufferSize = 512;

constexpr float kInitialLeftPanelRatio = 0.20f;
constexpr float kInitialRightPanelRatio = 0.20f;
constexpr float kInitialProjectBoundaryRatio = 0.50f;
constexpr float kInitialProblemsPanelHeight = 220.0f;
constexpr float kMinPanelRatio = 0.10f;
constexpr float kMaxPanelRatio = 0.40f;
constexpr float kSplitterThickness = 4.0f;
constexpr float kMinLeftSectionHeight = 120.0f;
constexpr float kMinCenterSectionHeight = 220.0f;
constexpr float kMinProblemsPanelHeight = 160.0f;

const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoResize
                                   | ImGuiWindowFlags_NoCollapse
                                   | ImGuiWindowFlags_NoBringToFrontOnFocus
                                   | ImGuiWindowFlags_NoSavedSettings;

enum class ProjectFileCreateKind {
    Sacm,
    EvidenceRegister,
    J3377CaeRegister
};

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0) return;
    size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

const char* ProjectFileCreateTitle(ProjectFileCreateKind kind) {
    switch (kind) {
        case ProjectFileCreateKind::Sacm: return "New GSN / SACM File";
        case ProjectFileCreateKind::EvidenceRegister: return "New Evidence Register";
        case ProjectFileCreateKind::J3377CaeRegister: return "New J3377 CAE Register";
    }
    return "New Project File";
}

void RenderLanguageMenu() {
    if (!ImGui::BeginMenu(ui::Tr(ui::MessageId::Language))) return;

    const ui::Language current = ui::CurrentLanguage();
    if (ImGui::MenuItem(ui::Tr(ui::MessageId::English), nullptr, current == ui::Language::English)) {
        ui::SetCurrentLanguage(ui::Language::English);
    }
    if (ImGui::MenuItem(ui::Tr(ui::MessageId::Japanese), nullptr, current == ui::Language::Japanese)) {
        ui::SetCurrentLanguage(ui::Language::Japanese);
    }

    ImGui::EndMenu();
}

void RenderThemeMenu() {
    if (!ImGui::BeginMenu(ui::Tr(ui::MessageId::Theme))) return;

    HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
    if (!runner_params) {
        ImGui::EndMenu();
        return;
    }

    for (int i = 0; i < ImGuiTheme::ImGuiTheme_Count; ++i) {
        auto theme = static_cast<ImGuiTheme::ImGuiTheme_>(i);
        bool selected = runner_params->imGuiWindowParams.tweakedTheme.Theme == theme;
        if (ImGui::MenuItem(ImGuiTheme::ImGuiTheme_Name(theme), nullptr, selected)) {
            runner_params->imGuiWindowParams.tweakedTheme.Theme = theme;
            ImGuiTheme::ApplyTweakedTheme(runner_params->imGuiWindowParams.tweakedTheme);
        }
    }

    ImGui::EndMenu();
}

std::string LowercaseAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

core::ProblemItem MakeAiReviewProblem(const std::string& id,
                                      core::ProblemSeverity severity,
                                      const std::string& element_id,
                                      const std::string& type,
                                      const std::string& message,
                                      const std::string& guideline_id = {}) {
    core::ProblemItem problem;
    problem.id = id;
    problem.severity = severity;
    problem.source = core::ProblemSource::AIReview;
    problem.element_id = element_id;
    problem.type = type;
    problem.message = message;
    problem.guideline_id = guideline_id;
    return problem;
}

std::string TruncateForProblemMessage(const std::string& value, size_t limit = 400) {
    if (value.size() <= limit) return value;
    return value.substr(0, limit) + "...";
}

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(path).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

std::filesystem::path FindGuidelinesFile() {
    const std::filesystem::path executable_dir = ExecutableDirectory();
    const std::filesystem::path current_dir = std::filesystem::current_path();
    const std::vector<std::filesystem::path> candidates = {
        executable_dir / "data" / "guidelines.yaml",
        current_dir / "data" / "guidelines.yaml",
        current_dir / "external" / "safety-case-core-guidelines" / "data" / "guidelines.yaml",
        current_dir.parent_path() / "external" / "safety-case-core-guidelines" / "data" / "guidelines.yaml",
    };

    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) return candidate;
    }
    return {};
}

bool IsProjectManifestPath(const std::filesystem::path& path) {
    return LowercaseAscii(path.filename().string()) == "af.proj";
}

ui::panels::RecentProjectEntry MakeRecentProjectEntry(const core::AppState& app_state) {
    ui::panels::RecentProjectEntry entry;
    if (!app_state.current_project.has_value()) return entry;

    const core::AssuranceProject& project = app_state.current_project.value();
    entry.name = project.name;
    entry.path = core::ProjectService::ManifestPath(project).u8string();

    if (!app_state.loaded_case.has_value()) return entry;
    for (const parser::SacmElement& element : app_state.loaded_case->elements) {
        const std::string type = LowercaseAscii(element.type);
        if (type == "claim") {
            ++entry.claims;
        } else if (type == "argumentreasoning") {
            ++entry.strategies;
        } else if (type == "artifact" || type == "artifactreference") {
            ++entry.evidence;
        }
        if (element.undeveloped) ++entry.undeveloped;
    }

    return entry;
}

}  // namespace

struct AppRuntime::Impl {
    Impl();

    core::AppState app_state;
    core::ProblemsManager problems_manager;

    std::shared_ptr<ai::AiSettingsStore> ai_settings_store;
    std::shared_ptr<ai::ISecretStore> ai_secret_store;
    std::shared_ptr<ai::IHttpClient> ai_http_client;
    std::shared_ptr<ai::IAiProvider> ai_provider;
    std::shared_ptr<ai::AiService> ai_service;
    ai::AiTaskRunner ai_task_runner;
    std::shared_ptr<ai::AiTaskHandle> ai_test_task;
    std::shared_ptr<ai::AiTaskHandle> ai_review_task;
    ai::AiProviderSettings ai_settings;
    ai::AiConnectionStatus ai_connection_status;
    bool ai_key_stored = false;
    bool ai_secure_store_available = false;
    char ai_api_key_buf[256] = {};
    char ai_model_buf[128] = {};

    char file_path_buf[kPathBufferSize] = "data/oasc-ja.xml";
    char dir_path_buf[kPathBufferSize] = "data";

    std::vector<std::string> xml_files;
    int selected_file_idx = -1;

    bool tree_needs_rebuild = false;
    core::AssuranceTree current_tree;
    bool show_overwrite_confirm = false;
    bool force_center_tab_selection = false;
    bool pending_focus_root = false;
    bool show_gsn_tab = true;
    bool show_cse_tab = false;
    bool show_evidence_tab = false;

    float left_ratio = kInitialLeftPanelRatio;
    float right_ratio = kInitialRightPanelRatio;
    float project_boundary_ratio = kInitialProjectBoundaryRatio;
    float problems_panel_height = kInitialProblemsPanelHeight;

    // Modal for unimplemented features
    bool show_not_implemented_modal = false;
    std::string not_implemented_feature;
    bool show_startup_project_window = true;

    bool show_create_project_modal = false;
    bool show_project_file_name_modal = false;
    ProjectFileCreateKind pending_project_file_kind = ProjectFileCreateKind::Sacm;
    char project_name_buf[128] = "MySafetyCase";
    char project_parent_buf[kPathBufferSize] = ".";
    char open_project_path_buf[kPathBufferSize] = "";
    char project_file_name_buf[256] = "main.sacm";
    std::vector<ui::panels::RecentProjectEntry> recent_projects;
    bool show_save_before_exit_modal = false;
    bool close_requested = false;

    // Modal for confirming a multi-element removal. Populated by RemoveSelected
    // when the planned removal targets more than one element.
    bool show_remove_confirm = false;
    std::string pending_remove_id;
    core::RemoveMode pending_remove_mode = core::RemoveMode::NodeOnly;
    std::vector<std::string> pending_remove_ids;

    bool show_preferences_window = false;
    bool show_theme_tweak_window = false;

    bool show_ai_review_debug_modal = false;
    ai::AiReviewRequestArtifacts pending_ai_review;
    std::string pending_ai_review_element_id;
    std::string pending_ai_review_element_type;
    std::string last_ai_review_raw_response;
    std::string last_ai_review_parse_error;

    void LoadAiSettingsState();
    void RefreshStoredAiKeyState();
};

AppRuntime::Impl::Impl()
    : ai_settings_store(std::make_shared<ai::AiSettingsStore>()),
      ai_secret_store(ai::CreatePlatformSecretStore()),
      ai_http_client(std::make_shared<ai::LibCurlHttpClient>()),
      ai_provider(std::make_shared<ai::OpenAiProvider>(ai_http_client)),
      ai_service(std::make_shared<ai::AiService>(ai_settings_store, ai_secret_store, ai_provider)) {
    LoadAiSettingsState();
}

void AppRuntime::Impl::LoadAiSettingsState() {
    std::string warning;
    ai_settings = ai_service->LoadSettings(&warning);
    CopyToBuffer(ai_model_buf, sizeof(ai_model_buf), ai_settings.model);
    ai_secure_store_available = ai_secret_store && ai_secret_store->IsAvailable();
    RefreshStoredAiKeyState();
    if (!warning.empty()) {
        ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, warning);
    }
}

void AppRuntime::Impl::RefreshStoredAiKeyState() {
    ai_key_stored = ai_service && ai_service->HasStoredApiKey();
}

void NormalizeCenterViewSelection(bool& show_gsn_tab,
                                  bool& show_cse_tab,
                                  bool& show_evidence_tab,
                                  bool& force_center_tab_selection,
                                  ui::CenterView& center_view) {
    if (!show_gsn_tab && !show_cse_tab && !show_evidence_tab) {
        // Keep at least one center tab visible.
        show_gsn_tab = true;
    }

    auto is_tab_visible = [&](ui::CenterView view) {
        switch (view) {
            case ui::CenterView::GsnCanvas: return show_gsn_tab;
            case ui::CenterView::CseRegister: return show_cse_tab;
            case ui::CenterView::EvidenceRegister: return show_evidence_tab;
        }
        return false;
    };

    if (!is_tab_visible(center_view)) {
        if (show_gsn_tab) {
            center_view = ui::CenterView::GsnCanvas;
        } else if (show_cse_tab) {
            center_view = ui::CenterView::CseRegister;
        } else {
            center_view = ui::CenterView::EvidenceRegister;
        }
        force_center_tab_selection = true;
    }
}

ui::ElementContextActions MakeElementContextActions(AppRuntime& runtime) {
    return ui::ElementContextActions{
        [&runtime](core::NewElementKind kind) { runtime.AddChildToSelected(kind); },
        [&runtime]() { runtime.AddTopGoal(); },
        [&runtime](core::RemoveMode mode) { runtime.RemoveSelected(mode); },
        [&runtime](const char* feature) {
            if (feature) runtime.ShowNotImplementedModal(feature);
        },
    };
}

AppRuntime::AppRuntime() : impl_(new Impl()) {
    impl_->current_tree = core::AssuranceTree();
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(nullptr);

    ui::UiState& ui_state = ui::GetUiState();
    ui_state.center_view = ui::CenterView::GsnCanvas;
    ui_state.selected_element_id.clear();
    ui_state.selected_problem_id.clear();
    ui_state.selected_problem_element_id.clear();
}

AppRuntime::~AppRuntime() {
    delete impl_;
}

void AppRuntime::RequestClose() {
    impl_->close_requested = true;
}

bool AppRuntime::AddChildToSelected(core::NewElementKind kind) {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return false;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("No element selected.");
        return false;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                          ? &impl_->app_state.sacm_package.value()
                                          : nullptr;

    std::string new_id;
    std::string error;
    if (!core::AddChildElement(ac, pkg, selected_id, kind, new_id, error)) {
        SetStatus("Add failed: " + error);
        return false;
    }

    impl_->tree_needs_rebuild = true;
    ui::UiState& s = ui::GetUiState();
    s.selected_element_id = new_id;
    s.center_on_selection = true;
    impl_->app_state.mark_dirty();
    SetStatus("Added " + new_id);
    return true;
}

bool AppRuntime::AddTopGoal() {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return false;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                          ? &impl_->app_state.sacm_package.value()
                                          : nullptr;

    std::string new_id;
    std::string error;
    if (!core::AddTopGoal(ac, pkg, new_id, error)) {
        SetStatus("Add failed: " + error);
        return false;
    }

    impl_->tree_needs_rebuild = true;
    ui::UiState& s = ui::GetUiState();
    s.selected_element_id = new_id;
    s.center_on_selection = true;
    impl_->app_state.mark_dirty();
    SetStatus("Added " + new_id);
    return true;
}

void AppRuntime::RemoveSelected(core::RemoveMode mode) {
    if (!impl_->app_state.loaded_case.has_value()) {
        SetStatus("No assurance case loaded.");
        return;
    }
    const std::string& selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        SetStatus("No element selected.");
        return;
    }

    parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
    auto planned = core::PlanRemoval(ac, selected_id, mode);
    if (planned.empty()) {
        SetStatus("Nothing to remove for this selection.");
        return;
    }

    sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                          ? &impl_->app_state.sacm_package.value()
                                          : nullptr;

    // Single-element removal: act immediately, no confirmation.
    if (planned.size() == 1) {
        std::string error;
        if (!core::RemoveElement(ac, pkg, selected_id, mode, error)) {
            SetStatus("Remove failed: " + error);
            return;
        }
        impl_->tree_needs_rebuild = true;
        ui::GetUiState().selected_element_id.clear();
        impl_->app_state.mark_dirty();
        SetStatus("Removed " + selected_id);
        return;
    }

    // Multi-element removal: stash the plan, mark nodes on the canvas, request
    // a fit-to-view of the marked set, and open the confirmation modal.
    impl_->show_remove_confirm = true;
    impl_->pending_remove_id = selected_id;
    impl_->pending_remove_mode = mode;
    impl_->pending_remove_ids.assign(planned.begin(), planned.end());

    auto& s = ui::GetUiState();
    s.marked_for_removal = std::move(planned);
    s.center_on_marked = true;
}

void AppRuntime::SetStatus(const std::string& message) {
    impl_->app_state.status_message = message;
}

void AppRuntime::ShowNotImplementedModal(const std::string& feature) {
    impl_->show_not_implemented_modal = true;
    impl_->not_implemented_feature = feature;
}

void AppRuntime::ScanDirectory() {
    impl_->xml_files.clear();
    impl_->selected_file_idx = -1;

    std::error_code ec;
    if (!std::filesystem::is_directory(impl_->dir_path_buf, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(impl_->dir_path_buf, ec)) {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".xml") {
            impl_->xml_files.push_back(entry.path().string());
        }
    }

    std::sort(impl_->xml_files.begin(), impl_->xml_files.end());

    std::error_code path_ec;
    std::filesystem::path selected_path = std::filesystem::weakly_canonical(std::filesystem::path(impl_->file_path_buf), path_ec);
    if (path_ec) {
        selected_path = std::filesystem::path(impl_->file_path_buf).lexically_normal();
    }

    for (int i = 0; i < static_cast<int>(impl_->xml_files.size()); ++i) {
        std::filesystem::path candidate_path = std::filesystem::weakly_canonical(std::filesystem::path(impl_->xml_files[i]), path_ec);
        if (path_ec) {
            path_ec.clear();
            candidate_path = std::filesystem::path(impl_->xml_files[i]).lexically_normal();
        }

        if (candidate_path == selected_path) {
            impl_->selected_file_idx = i;
            break;
        }
    }
}

void AppRuntime::RebuildDerivedViewsIfNeeded() {
    if (impl_->tree_needs_rebuild && !impl_->app_state.loaded_case.has_value()) {
        ui::RebuildRegisterViews(nullptr);
        impl_->tree_needs_rebuild = false;
        return;
    }

    if (!impl_->app_state.loaded_case.has_value() || !impl_->tree_needs_rebuild) {
        return;
    }

    const auto& ac = impl_->app_state.loaded_case.value();
    impl_->current_tree = ui::gsn::BuildAssuranceTree(ac);
    ui::gsn::SetCanvasTree(impl_->current_tree);
    ui::RebuildRegisterViews(&ac);
    ui::GetUiState().model_has_translations = ui::ModelHasTranslations(ac);

    if (impl_->pending_focus_root && impl_->current_tree.root) {
        ui::UiState& ui_state = ui::GetUiState();
        ui_state.selected_element_id = impl_->current_tree.root->id;
        ui_state.center_on_selection = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->force_center_tab_selection = true;
        impl_->pending_focus_root = false;
    }

    impl_->tree_needs_rebuild = false;
}

float AppRuntime::RenderMainMenuBar(bool& done) {
    if (!ImGui::BeginMainMenuBar()) {
        return 0.0f;
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::FileMenu))) {
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::CreateEmptyProject))) {
            BeginCreateProject();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::OpenProject))) {
            BeginOpenProject();
        }
        ImGui::Separator();
        bool has_project = impl_->app_state.current_project.has_value();
        if (!has_project) ImGui::BeginDisabled();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::SaveProject))) {
            SaveProject();
        }
        if (!has_project) ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::Exit))) {
            RequestExit(done);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::AddMenu))) {
        bool has_project = impl_->app_state.current_project.has_value();
        if (!has_project) ImGui::BeginDisabled();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewGsnSacmFile))) {
            BeginCreateProjectSacmFile();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewEvidenceRegister))) {
            BeginCreateProjectEvidenceRegister();
        }
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::NewJ3377CaeRegister))) {
            BeginCreateProjectJ3377CaeRegister();
        }
        if (!has_project) ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::EditMenu))) {
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::Preferences))) {
            impl_->show_preferences_window = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ui::Tr(ui::MessageId::ViewMenu))) {
        ui::UiState& ui_state = ui::GetUiState();
        ImGui::MenuItem(ui::Tr(ui::MessageId::GsnCanvas), nullptr, &impl_->show_gsn_tab);
        ImGui::MenuItem(ui::Tr(ui::MessageId::CseRegister), nullptr, &impl_->show_cse_tab);
        ImGui::MenuItem(ui::Tr(ui::MessageId::EvidenceRegister), nullptr, &impl_->show_evidence_tab);
        NormalizeCenterViewSelection(
            impl_->show_gsn_tab,
            impl_->show_cse_tab,
            impl_->show_evidence_tab,
            impl_->force_center_tab_selection,
            ui_state.center_view);

        ImGui::Separator();
        if (ImGui::BeginMenu(ui::Tr(ui::MessageId::Appearance))) {
            if (ImGui::MenuItem(ui::Tr(ui::MessageId::ThemeTweaks))) {
                impl_->show_theme_tweak_window = true;
            }
            RenderThemeMenu();
            RenderLanguageMenu();
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(ui::Tr(ui::MessageId::WelcomeScreen))) {
            impl_->show_startup_project_window = true;
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
    return ImGui::GetFrameHeight();
}

void AppRuntime::RenderPreferencesWindow() {
    if (!impl_->show_preferences_window) return;

    bool test_running = false;
    if (impl_->ai_test_task) {
        ai::AiTaskSnapshot snapshot = impl_->ai_test_task->Snapshot();
        test_running = snapshot.state == ai::AiTaskState::Running;
        impl_->ai_connection_status = snapshot.status;
        if (!test_running) {
            impl_->ai_test_task.reset();
            impl_->RefreshStoredAiKeyState();
        }
    }

    ui::panels::PreferencesPanelModel model;
    model.settings = &impl_->ai_settings;
    model.keyStored = impl_->ai_key_stored;
    model.secureStoreAvailable = impl_->ai_secure_store_available;
    model.testRunning = test_running;
    model.connectionStatus = impl_->ai_connection_status;
    model.apiKeyBuffer = impl_->ai_api_key_buf;
    model.apiKeyBufferSize = sizeof(impl_->ai_api_key_buf);
    model.modelBuffer = impl_->ai_model_buf;
    model.modelBufferSize = sizeof(impl_->ai_model_buf);
    model.language = ui::CurrentLanguage();

    ui::panels::PreferencesPanelCallbacks callbacks;
    callbacks.save_settings = [this](const ai::AiProviderSettings& settings) {
        impl_->ai_settings = settings;
        if (impl_->ai_settings.model.empty()) impl_->ai_settings.model = ai::kDefaultOpenAiModel;
        std::string error;
        if (!impl_->ai_service->SaveSettings(impl_->ai_settings, error)) {
            impl_->ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        CopyToBuffer(impl_->ai_model_buf, sizeof(impl_->ai_model_buf), impl_->ai_settings.model);
        impl_->ai_connection_status = ai::SuccessStatus("AI settings saved.");
    };
    callbacks.save_api_key = [this](const char* api_key) {
        if (!api_key || api_key[0] == '\0') {
            impl_->ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::MissingApiKey, "Enter an API key before saving.");
            return;
        }
        ai::SecretStoreResult result = impl_->ai_service->SaveApiKey(api_key);
        std::memset(impl_->ai_api_key_buf, 0, sizeof(impl_->ai_api_key_buf));
        impl_->RefreshStoredAiKeyState();
        impl_->ai_connection_status = result.success
            ? ai::SuccessStatus("API key saved securely.")
            : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.remove_api_key = [this]() {
        ai::SecretStoreResult result = impl_->ai_service->DeleteApiKey();
        std::memset(impl_->ai_api_key_buf, 0, sizeof(impl_->ai_api_key_buf));
        impl_->RefreshStoredAiKeyState();
        impl_->ai_connection_status = result.success
            ? ai::SuccessStatus("API key removed.")
            : ai::ErrorStatus(result.errorCode, result.errorMessage);
    };
    callbacks.test_connection = [this]() {
        if (impl_->ai_test_task && impl_->ai_test_task->IsRunning()) return;
        impl_->ai_connection_status = ai::MakeStatus(ai::AiTaskState::Running, ai::AiErrorCode::None, "Testing connection...");
        impl_->ai_settings.model = impl_->ai_model_buf;
        if (impl_->ai_settings.model.empty()) impl_->ai_settings.model = ai::kDefaultOpenAiModel;
        std::string error;
        if (!impl_->ai_service->SaveSettings(impl_->ai_settings, error)) {
            impl_->ai_connection_status = ai::ErrorStatus(ai::AiErrorCode::SettingsError, error);
            return;
        }
        std::shared_ptr<ai::AiService> service = impl_->ai_service;
        impl_->ai_test_task = impl_->ai_task_runner.RunConnectionTest([service]() {
            return service->TestConnection();
        });
    };
    callbacks.set_language = [](ui::Language language) {
        ui::SetCurrentLanguage(language);
    };

    ui::panels::ShowPreferencesWindow(impl_->show_preferences_window, model, callbacks);
}

void AppRuntime::RenderThemeTweaksWindow() {
    if (!impl_->show_theme_tweak_window) return;
    HelloImGui::ShowThemeTweakGuiWindow(&impl_->show_theme_tweak_window);
}

void AppRuntime::RenderSplitters(float display_w, float content_h, float left_w, float center_w, float top_y) {
    ui::widgets::DrawVerticalSplitter("##left_splitter",
                             left_w,
                             kSplitterThickness,
                             content_h,
                             top_y,
                             display_w,
                             impl_->left_ratio,
                             false,
                             kMinPanelRatio,
                             kMaxPanelRatio,
                             kPanelFlags);

    float center_x = left_w + kSplitterThickness;
    ui::widgets::DrawVerticalSplitter("##right_splitter",
                             center_x + center_w,
                             kSplitterThickness,
                             content_h,
                             top_y,
                             display_w,
                             impl_->right_ratio,
                             true,
                             kMinPanelRatio,
                             kMaxPanelRatio,
                             kPanelFlags);

    float available_h = content_h - kSplitterThickness;
    if (available_h <= 0.0f) return;

    float min_ratio = kMinLeftSectionHeight / available_h;
    if (min_ratio > 0.30f) min_ratio = 0.30f;

        auto clamp_boundaries = [&]() {
            if (impl_->project_boundary_ratio < min_ratio) 
                impl_->project_boundary_ratio = min_ratio;
            if (impl_->project_boundary_ratio > 1.0f - min_ratio) 
                impl_->project_boundary_ratio = 1.0f - min_ratio;
        };

    clamp_boundaries();

    float splitter1_y = top_y + available_h * impl_->project_boundary_ratio;

    float delta1 = ui::widgets::DrawHorizontalSplitter("##left_h_splitter_1", 0.0f, splitter1_y, left_w, kSplitterThickness, kPanelFlags);
    if (delta1 != 0.0f) {
        impl_->project_boundary_ratio += delta1 / available_h;
        clamp_boundaries();
    }

    auto clamp_problems_height = [&]() {
        float min_problems_h = std::min(kMinProblemsPanelHeight, available_h * 0.5f);
        float min_center_h = std::min(kMinCenterSectionHeight, available_h - min_problems_h);
        float max_problems_h = std::max(min_problems_h, available_h - min_center_h);
        impl_->problems_panel_height = std::clamp(impl_->problems_panel_height, min_problems_h, max_problems_h);
    };

    clamp_problems_height();
    float center_panel_h = std::max(0.0f, available_h - impl_->problems_panel_height);
    float center_splitter_y = top_y + center_panel_h;
    float delta_center = ui::widgets::DrawHorizontalSplitter(
        "##center_problems_splitter",
        center_x,
        center_splitter_y,
        center_w,
        kSplitterThickness,
        kPanelFlags);
    if (delta_center != 0.0f) {
        impl_->problems_panel_height -= delta_center;
        clamp_problems_height();
    }
}

void AppRuntime::RenderTreePanel(float left_w, float safety_tree_h, float top_y) {
    ImGui::SetNextWindowPos(ImVec2(0, top_y));
    ImGui::SetNextWindowSize(ImVec2(left_w, safety_tree_h));
    ImGui::Begin("Safety Case Tree", nullptr, kPanelFlags);
    ui::UiState& ui_state = ui::GetUiState();
    ui::ElementContextActions actions = MakeElementContextActions(*this);
    ui::ShowTreeViewPanel(impl_->current_tree.root ? &impl_->current_tree : nullptr,
                          GetLoadedCase(),
                          ui_state,
                          actions);
    ImGui::End();
}

void AppRuntime::RenderSacmViewerPanel(float left_w, float sacm_h, float top_y) {
    ui::panels::SacmViewerPanelModel model{
        impl_->app_state,
        impl_->dir_path_buf,
        sizeof(impl_->dir_path_buf),
        impl_->file_path_buf,
        sizeof(impl_->file_path_buf),
        impl_->xml_files,
        impl_->selected_file_idx,
        impl_->show_overwrite_confirm,
    };
    ui::panels::SacmViewerPanelCallbacks callbacks{
        [this]() { ScanDirectory(); },
        [this]() {
            impl_->tree_needs_rebuild = true;
            impl_->pending_focus_root = true;
        },
        [this]() {
            impl_->current_tree = core::AssuranceTree();
            ui::gsn::SetCanvasTree(impl_->current_tree);
            ui::RebuildRegisterViews(nullptr);
            ui::GetUiState().selected_element_id.clear();
        },
    };
    ui::panels::ShowSacmViewerPanel(left_w, sacm_h, top_y, kPanelFlags, model, callbacks);
}

void AppRuntime::RenderCenterPanel(float center_x, float center_w, float content_h, float top_y) {
    ImGui::SetNextWindowPos(ImVec2(center_x, top_y));
    ImGui::SetNextWindowSize(ImVec2(center_w, content_h));
    ImGui::Begin("Center View", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar);

    ui::UiState& ui_state = ui::GetUiState();
    NormalizeCenterViewSelection(
        impl_->show_gsn_tab,
        impl_->show_cse_tab,
        impl_->show_evidence_tab,
        impl_->force_center_tab_selection,
        ui_state.center_view);

    if (ImGui::BeginTabBar("##center_tabs")) {
        if (impl_->show_gsn_tab) {
            ImGuiTabItemFlags gsn_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::GsnCanvas)
                                          ? ImGuiTabItemFlags_SetSelected
                                          : 0;
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::GsnCanvas), nullptr, gsn_flags)) {
                ui_state.center_view = ui::CenterView::GsnCanvas;
                ui::ElementContextActions actions = MakeElementContextActions(*this);
                ui::gsn::ShowGsnCanvasContent(ui_state, GetLoadedCase(), actions);
                ImGui::EndTabItem();
            }
        }

        if (impl_->show_cse_tab) {
            ImGuiTabItemFlags cse_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::CseRegister)
                                          ? ImGuiTabItemFlags_SetSelected
                                          : 0;
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::CseRegister), nullptr, cse_flags)) {
                ui_state.center_view = ui::CenterView::CseRegister;
                if (impl_->app_state.active_project_file_role == core::ProjectFileRole::J3377CaeRegister) {
                    ImGui::TextWrapped("J3377 CAE register file: %s",
                                       impl_->app_state.active_project_file_path.string().c_str());
                    ImGui::TextDisabled("Editable CAE register content will be implemented in a later workflow.");
                    ImGui::Separator();
                }
                ui::ShowCseRegisterView();
                ImGui::EndTabItem();
            }
        }

        if (impl_->show_evidence_tab) {
            ImGuiTabItemFlags evidence_flags = (impl_->force_center_tab_selection && ui_state.center_view == ui::CenterView::EvidenceRegister)
                                               ? ImGuiTabItemFlags_SetSelected
                                               : 0;
            if (ImGui::BeginTabItem(ui::Tr(ui::MessageId::EvidenceRegister), nullptr, evidence_flags)) {
                ui_state.center_view = ui::CenterView::EvidenceRegister;
                if (impl_->app_state.active_project_file_role == core::ProjectFileRole::EvidenceRegister) {
                    ImGui::TextWrapped("Evidence register file: %s",
                                       impl_->app_state.active_project_file_path.string().c_str());
                    ImGui::TextDisabled("Editable evidence register content will be implemented in a later workflow.");
                    ImGui::Separator();
                }
                ui::ShowEvidenceRegisterView();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
        impl_->force_center_tab_selection = false;
    }

    ImGui::End();
}

void AppRuntime::RenderProblemsPanel(float center_x, float center_w, float problems_h, float top_y) {
    ui::panels::ProblemsPanelModel model{
        impl_->problems_manager,
        ui::GetUiState(),
        impl_->ai_review_task && impl_->ai_review_task->IsRunning() && !impl_->pending_ai_review.prompt.empty(),
    };
    ui::panels::ProblemsPanelCallbacks callbacks{
        [this](const core::ProblemItem& problem) {
            if (problem.element_id.empty()) return;
            ui::GetUiState().selected_problem_element_id = problem.element_id;
            SetStatus("Problem targets element " + problem.element_id + ". Element focus will be added in a later workflow.");
        },
        [this]() { BeginAiReviewForSelection(); },
    };
    ui::panels::ShowProblemsPanel(center_x, center_w, problems_h, top_y, kPanelFlags, model, callbacks);
}

void AppRuntime::BeginAiReviewForSelection() {
    if (impl_->ai_review_task && impl_->ai_review_task->IsRunning()) {
        SetStatus("AI review is already running.");
        return;
    }

    const std::string selected_element_id = ui::GetUiState().selected_element_id;
    if (selected_element_id.empty()) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:no-selection",
            core::ProblemSeverity::Info,
            {},
            "AI Review",
            "No GSN element is selected for AI review."));
        SetStatus("No GSN element is selected for AI review.");
        return;
    }

    const parser::AssuranceCase* assurance_case = GetLoadedCase();
    if (!assurance_case) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":no-loaded-case",
            core::ProblemSeverity::Error,
            selected_element_id,
            "AI Review",
            "No assurance case is loaded for AI review."));
        SetStatus("No assurance case is loaded for AI review.");
        return;
    }

    const parser::SacmElement* selected_element = ai::FindSacmElement(*assurance_case, selected_element_id);
    if (!selected_element) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":missing-element",
            core::ProblemSeverity::Error,
            selected_element_id,
            "AI Review",
            "Selected element was not found."));
        SetStatus("Selected element was not found.");
        return;
    }

    if (!ai::IsSupportedAiReviewElement(*selected_element)) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":unsupported-type",
            core::ProblemSeverity::Info,
            selected_element_id,
            ai::AiReviewElementType(*selected_element),
            "AI Review currently supports GSN Goal / SACM Claim elements only."));
        SetStatus("AI Review currently supports GSN Goal / SACM Claim elements only.");
        return;
    }

    ai::AiReviewPayload payload;
    std::string payload_error;
    if (!ai::BuildAiReviewPayload(*assurance_case, impl_->current_tree, selected_element_id, payload, payload_error)) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":payload-error",
            core::ProblemSeverity::Error,
            selected_element_id,
            ai::AiReviewElementType(*selected_element),
            payload_error.empty() ? "AI review payload could not be created." : payload_error));
        SetStatus("AI review payload could not be created.");
        return;
    }

    const std::filesystem::path guidelines_path = FindGuidelinesFile();
    if (guidelines_path.empty()) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":guidelines-missing",
            core::ProblemSeverity::Error,
            selected_element_id,
            payload.selected.type,
            "SCCG guidelines.yaml could not be found for AI review."));
        SetStatus("SCCG guidelines.yaml could not be found for AI review.");
        return;
    }

    parser::GuidelinesParseResult guidelines_result = parser::GuidelinesParser::ParseFile(guidelines_path.string());
    if (!guidelines_result.success) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":guidelines-parse-error",
            core::ProblemSeverity::Error,
            selected_element_id,
            payload.selected.type,
            "SCCG guidelines could not be parsed for AI review: " + guidelines_result.error_message));
        SetStatus("SCCG guidelines could not be parsed for AI review.");
        return;
    }

    std::vector<const parser::Guideline*> claim_guidelines = guidelines_result.document.FindGuidelinesByCategory("CL");
    if (claim_guidelines.empty()) {
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + selected_element_id + ":guidelines-empty",
            core::ProblemSeverity::Error,
            selected_element_id,
            payload.selected.type,
            "No SCCG CL guidelines were found for AI review."));
        SetStatus("No SCCG CL guidelines were found for AI review.");
        return;
    }

    impl_->pending_ai_review = ai::BuildAiReviewRequestArtifacts(payload, claim_guidelines);
    impl_->pending_ai_review_element_id = payload.selected.id;
    impl_->pending_ai_review_element_type = payload.selected.type;
    impl_->last_ai_review_raw_response.clear();
    impl_->last_ai_review_parse_error.clear();
    impl_->show_ai_review_debug_modal = true;
    SetStatus("AI review request is ready for inspection.");
}

void AppRuntime::StartPendingAiReviewRequest() {
    if (impl_->pending_ai_review.prompt.empty()) return;
    if (impl_->ai_review_task && impl_->ai_review_task->IsRunning()) return;

    ai::AiRequest request;
    request.systemInstruction = impl_->pending_ai_review.systemInstruction;
    request.userPrompt = impl_->pending_ai_review.prompt;

    std::shared_ptr<ai::AiService> service = impl_->ai_service;
    impl_->ai_review_task = impl_->ai_task_runner.RunGenerate([service, request]() {
        if (service) return service->Generate(request);
        ai::AiResponse response;
        response.success = false;
        response.errorCode = ai::AiErrorCode::Unknown;
        response.errorMessage = "AI service is unavailable.";
        return response;
    });
    SetStatus("AI review request sent.");
}

void AppRuntime::PollAiReviewTask() {
    if (!impl_->ai_review_task) return;

    ai::AiTaskSnapshot snapshot = impl_->ai_review_task->Snapshot();
    if (snapshot.state == ai::AiTaskState::Running) return;

    impl_->ai_review_task.reset();
    ai::AiResponse response = std::move(snapshot.response);
    if (!response.success) {
        std::string message = response.errorMessage.empty() ? ai::ToString(response.errorCode) : response.errorMessage;
        impl_->last_ai_review_raw_response = response.rawJson;
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + impl_->pending_ai_review_element_id + ":request-error",
            core::ProblemSeverity::Error,
            impl_->pending_ai_review_element_id,
            impl_->pending_ai_review_element_type,
            "AI review request failed: " + message));
        SetStatus("AI review request failed.");
        return;
    }

    impl_->last_ai_review_raw_response = response.text.empty() ? response.rawJson : response.text;
    ai::AiReviewParseResult parse_result = ai::ParseAiReviewResponse(response.text, impl_->pending_ai_review_element_id);
    if (!parse_result.success) {
        impl_->last_ai_review_parse_error = parse_result.errorMessage;
        std::string message = "AI response could not be parsed as the expected JSON format.";
        if (!parse_result.errorMessage.empty()) message += " " + parse_result.errorMessage;
        if (!impl_->last_ai_review_raw_response.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(impl_->last_ai_review_raw_response);
        }
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + impl_->pending_ai_review_element_id + ":parse-error",
            core::ProblemSeverity::Error,
            impl_->pending_ai_review_element_id,
            impl_->pending_ai_review_element_type,
            message));
        SetStatus("AI review response could not be parsed.");
        return;
    }

    if (!parse_result.reviewedElementId.empty() &&
        parse_result.reviewedElementId != impl_->pending_ai_review_element_id) {
        impl_->last_ai_review_parse_error =
            "AI response reviewed_element_id did not match the requested element.";
        std::string message = impl_->last_ai_review_parse_error;
        if (!impl_->last_ai_review_raw_response.empty()) {
            message += " Raw response: " + TruncateForProblemMessage(impl_->last_ai_review_raw_response);
        }
        impl_->problems_manager.AddOrUpdateProblem(MakeAiReviewProblem(
            "ai-review:" + impl_->pending_ai_review_element_id + ":parse-error",
            core::ProblemSeverity::Error,
            impl_->pending_ai_review_element_id,
            impl_->pending_ai_review_element_type,
            message));
        SetStatus("AI review response could not be validated.");
        return;
    }

    if (parse_result.reviewedElementType.empty()) parse_result.reviewedElementType = impl_->pending_ai_review_element_type;
    for (core::ProblemItem& problem : parse_result.problems) {
        if (problem.type.empty()) problem.type = parse_result.reviewedElementType;
    }

    impl_->problems_manager.ClearProblemsForElementAndSource(
        impl_->pending_ai_review_element_id, core::ProblemSource::AIReview);
    for (const core::ProblemItem& problem : parse_result.problems) {
        impl_->problems_manager.AddOrUpdateProblem(problem);
    }

    SetStatus(parse_result.problems.empty()
        ? "AI review completed with no findings."
        : "AI review completed with " + std::to_string(parse_result.problems.size()) + " finding(s).");
}

void AppRuntime::RenderAiReviewDebugModal() {
    if (!impl_->show_ai_review_debug_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(920.0f, 700.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("AI Review Debug Request", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("Inspect the exact AI review request data before sending.");
        ImGui::Spacing();

        const float child_height = std::max(240.0f, ImGui::GetContentRegionAvail().y - 58.0f);
        ImGui::BeginChild("##ai_review_debug_text", ImVec2(0.0f, child_height), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(impl_->pending_ai_review.debugText.c_str());
        ImGui::EndChild();
        ImGui::Spacing();

        const float button_width = 110.0f;
        if (ImGui::Button("OK", ImVec2(button_width, 0.0f))) {
            impl_->show_ai_review_debug_modal = false;
            ImGui::CloseCurrentPopup();
            StartPendingAiReviewRequest();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(button_width, 0.0f))) {
            impl_->show_ai_review_debug_modal = false;
            impl_->pending_ai_review = {};
            impl_->pending_ai_review_element_id.clear();
            impl_->pending_ai_review_element_type.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else if (impl_->show_ai_review_debug_modal) {
        ImGui::OpenPopup("AI Review Debug Request");
    }
}

void AppRuntime::RenderElementPropertiesPanel(float center_x, float center_w, float right_w, float content_h, float top_y) {
    float right_x = center_x + center_w + kSplitterThickness;
    ImGui::SetNextWindowPos(ImVec2(right_x, top_y));
    ImGui::SetNextWindowSize(ImVec2(right_w, content_h));
    ImGui::Begin("Element Properties", nullptr, kPanelFlags);

    parser::AssuranceCase* ac_ptr = impl_->app_state.loaded_case.has_value() ? &impl_->app_state.loaded_case.value() : nullptr;
    sacm::AssuranceCasePackage* sacm_ptr = impl_->app_state.sacm_package.has_value() ? &impl_->app_state.sacm_package.value() : nullptr;
    if (ui::panels::ShowElementPanel(ac_ptr, sacm_ptr)) {
        impl_->tree_needs_rebuild = true;
        impl_->app_state.mark_dirty();
    }

    ImGui::End();
}

void AppRuntime::RenderNotImplementedModal() {
    if (!impl_->show_not_implemented_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##not_implemented_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s is not implemented yet.", impl_->not_implemented_feature.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        float button_width = 100.0f;
        float modal_width = ImGui::GetWindowWidth();
        float center_x = (modal_width - button_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);
        if (ImGui::Button("OK", ImVec2(button_width, 0))) {
            impl_->show_not_implemented_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_not_implemented_modal) {
        ImGui::OpenPopup("##not_implemented_modal");
    }
}

void AppRuntime::RenderRemoveConfirmModal() {
    if (!impl_->show_remove_confirm) return;

    auto cancel = [&]() {
        impl_->show_remove_confirm = false;
        impl_->pending_remove_id.clear();
        impl_->pending_remove_ids.clear();
        ui::GetUiState().marked_for_removal.clear();
    };

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##remove_confirm_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int n = static_cast<int>(impl_->pending_remove_ids.size());
        const char* mode_label =
            impl_->pending_remove_mode == core::RemoveMode::NodeOnly
                ? "this node and its attachments"
                : "this node and its descendants";
        ImGui::Text("Remove %s?", mode_label);
        ImGui::Text("%d element%s will be deleted (highlighted in red).",
                    n, n == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::Spacing();

        const float button_width = 110.0f;
        const float spacing = 10.0f;
        const float total_width = button_width * 2.0f + spacing;
        const float center_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;
        ImGui::SetCursorPosX(center_x);

        if (ImGui::Button("Remove", ImVec2(button_width, 0))) {
            std::string id = impl_->pending_remove_id;
            core::RemoveMode mode = impl_->pending_remove_mode;
            cancel();
            ImGui::CloseCurrentPopup();

            if (impl_->app_state.loaded_case.has_value()) {
                parser::AssuranceCase& ac = impl_->app_state.loaded_case.value();
                sacm::AssuranceCasePackage* pkg = impl_->app_state.sacm_package.has_value()
                                                      ? &impl_->app_state.sacm_package.value()
                                                      : nullptr;
                std::string error;
                if (!core::RemoveElement(ac, pkg, id, mode, error)) {
                    SetStatus("Remove failed: " + error);
                } else {
                    impl_->tree_needs_rebuild = true;
                    ui::GetUiState().selected_element_id.clear();
                    impl_->app_state.mark_dirty();
                    SetStatus("Removed " + std::to_string(n) + " element" + (n == 1 ? "" : "s"));
                }
            }
        }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_remove_confirm) {
        ImGui::OpenPopup("##remove_confirm_modal");
    }
}

void AppRuntime::RenderStartupProjectWindow() {
    ui::panels::WelcomeModalCallbacks callbacks{
        [this]() { BeginCreateProject(); },
        [this]() { ShowNotImplementedModal("Create Assurance Project from Template"); },
        [this]() { BeginOpenProject(); },
        [this]() { ShowNotImplementedModal("Import SACM"); },
        [this](const ui::panels::RecentProjectEntry& entry) {
            if (!TryOpenProjectManifest(entry.path)) {
                RemoveRecentProject(impl_->recent_projects, entry.path);
            }
        },
    };
    ui::panels::ShowWelcomeModal(impl_->show_startup_project_window, impl_->recent_projects, callbacks);
}

void AppRuntime::BeginCreateProject() {
    std::string selected_path;
    std::string error_message;
    const dialogs::DialogResult result = dialogs::BrowseForProjectParentFolder(
        impl_->project_parent_buf, selected_path, error_message);
    if (result == dialogs::DialogResult::Selected) {
        CopyToBuffer(impl_->project_parent_buf, sizeof(impl_->project_parent_buf), selected_path);
        if (impl_->project_name_buf[0] == '\0') {
            CopyToBuffer(impl_->project_name_buf, sizeof(impl_->project_name_buf), "MySafetyCase");
        }
        impl_->show_create_project_modal = true;
    } else if (result == dialogs::DialogResult::Failed) {
        SetStatus("Browse failed: " + error_message);
    }
}

void AppRuntime::BeginOpenProject() {
    std::string default_path = impl_->open_project_path_buf;
    if (default_path.empty() && !impl_->recent_projects.empty()) {
        default_path = impl_->recent_projects.front().path;
    }

    std::string selected_path;
    std::string error_message;
    const dialogs::DialogResult result = dialogs::BrowseForProjectManifest(
        default_path, selected_path, error_message);
    if (result == dialogs::DialogResult::Selected) {
        CopyToBuffer(impl_->open_project_path_buf, sizeof(impl_->open_project_path_buf), selected_path);
        TryOpenProjectManifest(selected_path);
    } else if (result == dialogs::DialogResult::Failed) {
        SetStatus("Browse failed: " + error_message);
    }
}

void AppRuntime::TouchCurrentProjectRecent() {
    ui::panels::RecentProjectEntry entry = MakeRecentProjectEntry(impl_->app_state);
    if (!entry.path.empty()) {
        TouchRecentProject(impl_->recent_projects, std::move(entry));
    }
}

void AppRuntime::BeginCreateProjectSacmFile() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->pending_project_file_kind = ProjectFileCreateKind::Sacm;
    CopyToBuffer(impl_->project_file_name_buf, sizeof(impl_->project_file_name_buf), "main.sacm");
    impl_->show_project_file_name_modal = true;
}

void AppRuntime::BeginCreateProjectEvidenceRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->pending_project_file_kind = ProjectFileCreateKind::EvidenceRegister;
    CopyToBuffer(impl_->project_file_name_buf, sizeof(impl_->project_file_name_buf), "evidence-register.af.json");
    impl_->show_project_file_name_modal = true;
}

void AppRuntime::BeginCreateProjectJ3377CaeRegister() {
    if (!impl_->app_state.current_project.has_value()) {
        SetStatus("Create or open a project first.");
        return;
    }
    impl_->pending_project_file_kind = ProjectFileCreateKind::J3377CaeRegister;
    CopyToBuffer(impl_->project_file_name_buf, sizeof(impl_->project_file_name_buf), "j3377-cae-register.af.json");
    impl_->show_project_file_name_modal = true;
}

void AppRuntime::OpenProjectFile(const core::ProjectFileEntry& entry) {
    if (!impl_->app_state.open_project_file(entry)) return;

    ui::UiState& ui_state = ui::GetUiState();
    if (entry.role == core::ProjectFileRole::SacmArgument) {
        impl_->tree_needs_rebuild = true;
        impl_->pending_focus_root = true;
        impl_->show_gsn_tab = true;
        ui_state.center_view = ui::CenterView::GsnCanvas;
        impl_->force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::EvidenceRegister) {
        impl_->show_evidence_tab = true;
        ui_state.center_view = ui::CenterView::EvidenceRegister;
        impl_->force_center_tab_selection = true;
    } else if (entry.role == core::ProjectFileRole::J3377CaeRegister) {
        impl_->show_cse_tab = true;
        ui_state.center_view = ui::CenterView::CseRegister;
        impl_->force_center_tab_selection = true;
    }
}

bool AppRuntime::OpenFirstProjectSacmFile() {
    if (!impl_->app_state.current_project.has_value()) return false;

    for (const auto& entry : impl_->app_state.current_project->files) {
        if (entry.role != core::ProjectFileRole::SacmArgument) continue;
        if (entry.state == core::ProjectFileState::Missing) continue;
        if (impl_->app_state.open_project_file(entry)) {
            impl_->tree_needs_rebuild = true;
            impl_->pending_focus_root = true;
            impl_->show_gsn_tab = true;
            ui::UiState& ui_state = ui::GetUiState();
            ui_state.center_view = ui::CenterView::GsnCanvas;
            impl_->force_center_tab_selection = true;
            return true;
        }
    }

    SetStatus("Project opened, but no SACM file could be loaded.");
    return false;
}

bool AppRuntime::TryOpenProjectManifest(const std::string& selected_path) {
    std::filesystem::path manifest_path(selected_path);
    if (!IsProjectManifestPath(manifest_path)) {
        SetStatus("Please select an af.proj file.");
        return false;
    }
    if (!impl_->app_state.open_project(selected_path)) {
        return false;
    }
    OpenFirstProjectSacmFile();
    TouchCurrentProjectRecent();
    CopyToBuffer(impl_->open_project_path_buf, sizeof(impl_->open_project_path_buf), selected_path);
    ImGui::CloseCurrentPopup();
    return true;
}

void AppRuntime::RenderCreateProjectModal() {
    if (!impl_->show_create_project_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create Empty Assurance Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Project name");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##project_name", impl_->project_name_buf, sizeof(impl_->project_name_buf));

        ImGui::TextUnformatted("Parent location");
        ImGui::TextDisabled("%s", impl_->project_parent_buf);

        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
            if (impl_->app_state.create_empty_project(impl_->project_name_buf, impl_->project_parent_buf)) {
                OpenFirstProjectSacmFile();
                TouchCurrentProjectRecent();
                impl_->show_create_project_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            impl_->show_create_project_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_create_project_modal) {
        ImGui::OpenPopup("Create Empty Assurance Project");
    }
}

void AppRuntime::RenderProjectFileNameModal() {
    if (!impl_->show_project_file_name_modal) return;

    const char* title = ProjectFileCreateTitle(impl_->pending_project_file_kind);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("File name");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##project_file_name", impl_->project_file_name_buf, sizeof(impl_->project_file_name_buf));
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
            bool created = false;
            if (impl_->pending_project_file_kind == ProjectFileCreateKind::Sacm) {
                created = impl_->app_state.create_project_sacm_file(impl_->project_file_name_buf);
            } else if (impl_->pending_project_file_kind == ProjectFileCreateKind::EvidenceRegister) {
                created = impl_->app_state.create_project_evidence_register(impl_->project_file_name_buf);
            } else {
                created = impl_->app_state.create_project_j3377_cae_register(impl_->project_file_name_buf);
            }
            if (created) {
                impl_->show_project_file_name_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
            impl_->show_project_file_name_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_project_file_name_modal) {
        ImGui::OpenPopup(title);
    }
}

void AppRuntime::RenderProjectLoadReportModal() {
    auto& report = impl_->app_state.last_project_load_report;
    if (!report.showPopup) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Project Loading Status", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        for (const auto& step : report.steps) {
            const char* mark = "[OK]";
            if (step.status == core::ProjectLoadStepStatus::Failed) mark = "[FAIL]";
            if (step.status == core::ProjectLoadStepStatus::Warning) mark = "[WARN]";
            ImGui::Text("%s %s", mark, step.label.c_str());
            if (!step.message.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", step.message.c_str());
            }
        }
        if (!report.warnings.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("External changes detected");
            for (const auto& warning : report.warnings) {
                ImGui::BulletText("%s", warning.c_str());
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) {
            report.showPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (report.showPopup) {
        ImGui::OpenPopup("Project Loading Status");
    }
}

void AppRuntime::RenderSaveBeforeExitModal(bool& done) {
    if (!impl_->show_save_before_exit_modal) return;

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("You have unsaved changes. Save before closing?");
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            bool saved = false;
            if (impl_->app_state.current_project.has_value()) {
                saved = SaveProject();
            } else {
                saved = impl_->app_state.save_current_document();
            }

            if (saved) {
                impl_->show_save_before_exit_modal = false;
                done = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(100.0f, 0.0f))) {
            impl_->show_save_before_exit_modal = false;
            done = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            impl_->show_save_before_exit_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (impl_->show_save_before_exit_modal) {
        ImGui::OpenPopup("Unsaved Changes");
    }
}

bool AppRuntime::SaveProject() {
    return impl_->app_state.save_project();
}

void AppRuntime::RequestExit(bool& done) {
    if (impl_->app_state.has_unsaved_changes) {
        impl_->show_save_before_exit_modal = true;
        return;
    }
    done = true;
}

const parser::AssuranceCase* AppRuntime::GetLoadedCase() const {
    if (!impl_->app_state.loaded_case.has_value()) return nullptr;
    return &impl_->app_state.loaded_case.value();
}

void AppRuntime::LoadRecentProjectsPreference(const std::string& content) {
    impl_->recent_projects = app::LoadRecentProjectsPreference(content);
}

std::string AppRuntime::RecentProjectsPreferenceJson() const {
    return app::SaveRecentProjectsPreference(impl_->recent_projects);
}

void AppRuntime::RenderFrame(bool& done) {
    if (impl_->close_requested) {
        impl_->close_requested = false;
        RequestExit(done);
    }

    ImVec2 display = ImGui::GetIO().DisplaySize;
    float top_y = RenderMainMenuBar(done);

    float content_h = std::max(0.0f, display.y - top_y);

    float left_w = display.x * impl_->left_ratio;
    float right_w = display.x * impl_->right_ratio;
    float center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    RebuildDerivedViewsIfNeeded();
    PollAiReviewTask();

    RenderSplitters(display.x, content_h, left_w, center_w, top_y);

    left_w = display.x * impl_->left_ratio;
    right_w = display.x * impl_->right_ratio;
    center_w = display.x - left_w - right_w - kSplitterThickness * 2.0f;

    float available_h = std::max(0.0f, content_h - kSplitterThickness);
    float project_h = available_h * impl_->project_boundary_ratio;
    float safety_tree_h = std::max(0.0f, available_h - project_h);

    float project_y = top_y;
    float safety_y = project_y + project_h + kSplitterThickness;

    ui::panels::ProjectFilesPanelModel project_model{
        impl_->app_state.current_project.has_value() ? &impl_->app_state.current_project.value() : nullptr,
    };
    ui::panels::ProjectFilesPanelCallbacks project_callbacks{
        [this]() { BeginCreateProjectSacmFile(); },
        [this]() { BeginCreateProjectEvidenceRegister(); },
        [this]() { BeginCreateProjectJ3377CaeRegister(); },
        [this](const core::ProjectFileEntry& entry) { OpenProjectFile(entry); },
    };
    ui::panels::ShowProjectFilesPanel(left_w, project_h, project_y, kPanelFlags, project_model, project_callbacks);
    RenderTreePanel(left_w, safety_tree_h, safety_y);

    float center_x = left_w + kSplitterThickness;
    float center_available_h = std::max(0.0f, content_h - kSplitterThickness);
    float problems_h = std::min(impl_->problems_panel_height, center_available_h);
    float center_panel_h = std::max(0.0f, center_available_h - problems_h);
    float problems_y = top_y + center_panel_h + kSplitterThickness;

    RenderCenterPanel(center_x, center_w, center_panel_h, top_y);
    RenderProblemsPanel(center_x, center_w, problems_h, problems_y);
    RenderElementPropertiesPanel(center_x, center_w, right_w, content_h, top_y);

    RenderPreferencesWindow();
    RenderThemeTweaksWindow();
    RenderAiReviewDebugModal();

    RenderNotImplementedModal();
    RenderRemoveConfirmModal();
    RenderCreateProjectModal();
    RenderProjectFileNameModal();
    RenderProjectLoadReportModal();
    RenderSaveBeforeExitModal(done);
    RenderStartupProjectWindow();
}

}  // namespace app
