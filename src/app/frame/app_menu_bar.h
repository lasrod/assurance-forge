#pragma once

#include <functional>

namespace app {
struct AppRuntimeState;
}

namespace app::frame {

struct AppMenuBarCallbacks {
    std::function<void()> begin_create_project;
    std::function<void()> begin_open_project;
    std::function<bool()> save_project;
    std::function<void()> export_gsn_svg;
    std::function<void(bool&)> request_exit;
    std::function<void()> begin_create_project_sacm_file;
    std::function<void()> begin_create_project_evidence_register;
    std::function<void()> begin_create_project_j3377_cae_register;
    std::function<void()> undo;
    std::function<bool()> can_undo;
};

float RenderAppMenuBar(AppRuntimeState& state, bool& done, const AppMenuBarCallbacks& callbacks);

} // namespace app::frame