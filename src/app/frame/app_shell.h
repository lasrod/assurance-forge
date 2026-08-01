#pragma once

#include "app/frame/app_layout_regions.h"
#include "imgui.h"
#include "ui/ui_state.h"

namespace app {
struct AppRuntimeState;
}

namespace app::frame {

void NormalizeCenterViewSelection(AppRuntimeState& state, ui::CenterView& center_view);
AppLayoutRegions RenderAppShell(AppRuntimeState& state, float menu_height, ImGuiWindowFlags panel_flags);

} // namespace app::frame