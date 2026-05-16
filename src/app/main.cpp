// Assurance Forge application bootstrap.

#include "app/app_runtime.h"
#include "app/app_ui_bootstrap.h"
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "GLFW/glfw3.h"
#ifdef _WIN32
#include "GLFW/glfw3native.h"
#endif
#include "hello_imgui/hello_imgui.h"
#include "ui/localization.h"
#include "ui/theme.h"

namespace {

constexpr const char* kLanguagePreference = "AssuranceForge.Language";
constexpr const char* kRecentProjectsPreference = "AssuranceForge.RecentProjects";
constexpr const char* kReviewerNamePreference = "AssuranceForge.ReviewerName";

#ifdef _WIN32
void EnableDarkTitleBar(GLFWwindow* glfw_window) {
    if (glfw_window == nullptr) {
        return;
    }

    HWND hwnd = glfwGetWin32Window(glfw_window);
    if (hwnd == nullptr) {
        return;
    }

    BOOL use_dark_mode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &use_dark_mode, sizeof(use_dark_mode));

    COLORREF caption_color = RGB(10, 15, 24);
    COLORREF text_color = RGB(230, 235, 245);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption_color, sizeof(caption_color));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text_color, sizeof(text_color));
}
#endif

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
    params.callbacks.PostInit = [&runtime]() {
        ui::SetCurrentLanguage(ui::ParseLanguageCode(HelloImGui::LoadUserPref(kLanguagePreference)));
        runtime.LoadRecentProjectsPreference(HelloImGui::LoadUserPref(kRecentProjectsPreference));
        runtime.LoadReviewerNamePreference(HelloImGui::LoadUserPref(kReviewerNamePreference));
        HelloImGui::RunnerParams* post_init_runner_params = HelloImGui::GetRunnerParams();
        if (post_init_runner_params != nullptr) {
            const char* loaded_theme_name =
                ImGuiTheme::ImGuiTheme_Name(post_init_runner_params->imGuiWindowParams.tweakedTheme.Theme);
            ui::ApplyAppTheme(ui::ParseAppTheme(loaded_theme_name != nullptr ? loaded_theme_name : ""));
        }
#ifdef _WIN32
        if (post_init_runner_params != nullptr && post_init_runner_params->backendPointers.glfwWindow != nullptr) {
            EnableDarkTitleBar(static_cast<GLFWwindow*>(post_init_runner_params->backendPointers.glfwWindow));
        }
#endif
    };
    params.callbacks.BeforeExit = [&runtime]() {
        HelloImGui::SaveUserPref(kLanguagePreference, ui::LanguageCode(ui::CurrentLanguage()));
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
