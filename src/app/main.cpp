// Assurance Forge application bootstrap.

#include "app/app_runtime.h"
#include "app/app_ui_bootstrap.h"
#include "GLFW/glfw3.h"
#include "hello_imgui/hello_imgui.h"
#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include <filesystem>

namespace {

constexpr const char* kLanguagePreference = "AssuranceForge.Language";
constexpr const char* kRecentProjectsPreference = "AssuranceForge.RecentProjects";
constexpr const char* kReviewerNamePreference = "AssuranceForge.ReviewerName";

void CancelNativeWindowCloseRequest(HelloImGui::RunnerParams& runner_params) {
    runner_params.appShallExit = false;
    if (runner_params.backendPointers.glfwWindow != nullptr) {
        glfwSetWindowShouldClose(static_cast<GLFWwindow*>(runner_params.backendPointers.glfwWindow), GLFW_FALSE);
    }
}

} // namespace

int main(int, char**) {
    app::AppRuntime runtime;
    bool done = false;

    HelloImGui::RunnerParams params;
    params.appWindowParams.windowTitle = "Assurance Forge";
    params.appWindowParams.windowGeometry.size = {1280, 720};
    params.appWindowParams.resizable = true;
    params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::NoDefaultWindow;
    params.imGuiWindowParams.showMenuBar = false;
    params.imGuiWindowParams.rememberTheme = true;
    params.imGuiWindowParams.tweakedTheme.Theme = ImGuiTheme::ImGuiTheme_ImGuiColorsDark;
    params.iniFolderType = HelloImGui::IniFolderType::AppUserConfigFolder;
    params.iniFilename = "AssuranceForge/hello_imgui.ini";
    params.iniFilename_useAppWindowTitle = false;
    params.iniDisable = false;
    params.callbacks.SetupImGuiConfig = app::ConfigureImGuiConfig;
    params.callbacks.LoadAdditionalFonts = app::ConfigureImGuiFonts;
    params.callbacks.SetupImGuiStyle = []() {
        HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
        const char* loaded_theme_name = nullptr;
        if (runner_params != nullptr) {
            loaded_theme_name = ImGuiTheme::ImGuiTheme_Name(runner_params->imGuiWindowParams.tweakedTheme.Theme);
        }
        ui::ApplyAppTheme(ui::ParseAppTheme(loaded_theme_name != nullptr ? loaded_theme_name : ""));
    };
    params.callbacks.PostInit = [&runtime]() {
        ui::i18n::LocalizationConfig localization_config;
        // AssetFileFullPath resolves files, not directories, so derive the
        // locale directory from a bundled asset that is guaranteed to exist.
        const std::string assets_anchor = HelloImGui::AssetFileFullPath("fonts/NotoSansJP-Regular.otf", false);
        if (!assets_anchor.empty()) {
            localization_config.localeDirectory =
                std::filesystem::path(assets_anchor).parent_path().parent_path() / "locale";
        }
        localization_config.language = ui::i18n::ParseLanguageCode(HelloImGui::LoadUserPref(kLanguagePreference));
        ui::i18n::Initialize(localization_config);
        runtime.LoadRecentProjectsPreference(HelloImGui::LoadUserPref(kRecentProjectsPreference));
        runtime.LoadReviewerNamePreference(HelloImGui::LoadUserPref(kReviewerNamePreference));
    };
    params.callbacks.BeforeExit = [&runtime]() {
        HelloImGui::SaveUserPref(kLanguagePreference, ui::i18n::LanguageCode(ui::i18n::CurrentLanguage()));
        HelloImGui::SaveUserPref(kRecentProjectsPreference, runtime.RecentProjectsPreferenceJson());
        HelloImGui::SaveUserPref(kReviewerNamePreference, runtime.ReviewerNamePreference());
    };
    params.callbacks.ShowGui = [&]() {
        HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
        if (runner_params && runner_params->appShallExit && !done) {
            CancelNativeWindowCloseRequest(*runner_params);
            runtime.RequestClose();
        }

        runtime.RenderFrame(done);
        if (done && runner_params) {
            runner_params->appShallExit = true;
        }
    };

    HelloImGui::Run(params);
    return 0;
}
