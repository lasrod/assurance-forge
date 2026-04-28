// Assurance Forge application bootstrap.

#include "app/app_runtime.h"
#include "app/app_ui_bootstrap.h"

#include "hello_imgui/hello_imgui.h"

int main(int, char**) {
    app::AppRuntime runtime;
    bool done = false;

    HelloImGui::RunnerParams params;
    params.appWindowParams.windowTitle = "Assurance Forge";
    params.appWindowParams.windowGeometry.size = {1280, 720};
    params.appWindowParams.resizable = true;
    params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::NoDefaultWindow;
    params.imGuiWindowParams.showMenuBar = false;
    params.iniDisable = true;
    params.callbacks.SetupImGuiConfig = app::ConfigureImGuiConfig;
    params.callbacks.SetupImGuiStyle = app::ConfigureImGuiStyle;
    params.callbacks.LoadAdditionalFonts = app::ConfigureImGuiFonts;
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
