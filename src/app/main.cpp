// Assurance Forge application bootstrap.

#include "app/app_runtime.h"
#include "app/app_ui_bootstrap.h"
#include "GLFW/glfw3.h"
#include "hello_imgui/hello_imgui.h"
#include "ui/localization.h"
#include "ui/theme.h"

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
    bool app_theme_applied_after_runner_theme_load = false;

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
    params.callbacks.PostInit = [&runtime]() {
        ui::SetCurrentLanguage(ui::ParseLanguageCode(HelloImGui::LoadUserPref(kLanguagePreference)));
        runtime.LoadRecentProjectsPreference(HelloImGui::LoadUserPref(kRecentProjectsPreference));
        runtime.LoadReviewerNamePreference(HelloImGui::LoadUserPref(kReviewerNamePreference));
    };
    params.callbacks.BeforeExit = [&runtime]() {
        HelloImGui::SaveUserPref(kLanguagePreference, ui::LanguageCode(ui::CurrentLanguage()));
        HelloImGui::SaveUserPref(kRecentProjectsPreference, runtime.RecentProjectsPreferenceJson());
        HelloImGui::SaveUserPref(kReviewerNamePreference, runtime.ReviewerNamePreference());
    };
    params.callbacks.ShowGui = [&]() {
        HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
        if (!app_theme_applied_after_runner_theme_load) {
            const char* loaded_theme_name =
                ImGuiTheme::ImGuiTheme_Name(runner_params->imGuiWindowParams.tweakedTheme.Theme);
            ui::ApplyAppTheme(ui::ParseAppTheme(loaded_theme_name));
            app_theme_applied_after_runner_theme_load = true;
        }
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
