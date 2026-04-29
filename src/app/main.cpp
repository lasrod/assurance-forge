// Assurance Forge application bootstrap.

#include "app/app_runtime.h"
#include "app/app_ui_bootstrap.h"

#include "hello_imgui/hello_imgui.h"

#include "ui/localization.h"

namespace {

constexpr const char* kLanguagePreference = "AssuranceForge.Language";
constexpr const char* kRecentProjectsPreference = "AssuranceForge.RecentProjects";

}  // namespace

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
    params.imGuiWindowParams.tweakedTheme.Theme = ImGuiTheme::ImGuiTheme_DarculaDarker;
    params.iniFolderType = HelloImGui::IniFolderType::AppUserConfigFolder;
    params.iniFilename = "AssuranceForge/hello_imgui.ini";
    params.iniFilename_useAppWindowTitle = false;
    params.iniDisable = false;
    params.callbacks.SetupImGuiConfig = app::ConfigureImGuiConfig;
    params.callbacks.LoadAdditionalFonts = app::ConfigureImGuiFonts;
    params.callbacks.PostInit = [&runtime]() {
        ui::SetCurrentLanguage(ui::ParseLanguageCode(HelloImGui::LoadUserPref(kLanguagePreference)));
        runtime.LoadRecentProjectsPreference(HelloImGui::LoadUserPref(kRecentProjectsPreference));
    };
    params.callbacks.BeforeExit = [&runtime]() {
        HelloImGui::SaveUserPref(kLanguagePreference, ui::LanguageCode(ui::CurrentLanguage()));
        HelloImGui::SaveUserPref(kRecentProjectsPreference, runtime.RecentProjectsPreferenceJson());
    };
    params.callbacks.ShowGui = [&]() {
        HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
        if (runner_params && runner_params->appShallExit && !done) {
            runner_params->appShallExit = false;
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
