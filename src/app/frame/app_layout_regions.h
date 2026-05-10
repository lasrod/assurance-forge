#pragma once

#include "imgui.h"

namespace app {

struct AppLayoutRegion {
    ImVec2 pos{};
    ImVec2 size{};
};

struct AppLayoutRegions {
    AppLayoutRegion project_explorer;
    AppLayoutRegion argument_navigator;
    AppLayoutRegion workbench;
    AppLayoutRegion inspector;
    AppLayoutRegion feedback_dock;
    float menu_height = 0.0f;
};

} // namespace app