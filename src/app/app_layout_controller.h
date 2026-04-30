#pragma once

#include "app/app_runtime_state.h"
#include "imgui.h"
#include "ui/ui_state.h"

namespace app {

void NormalizeCenterViewSelection(AppRuntimeState& state, ui::CenterView& center_view);
void RenderAppSplitters(AppRuntimeState& state,
                        float display_w,
                        float content_h,
                        float left_w,
                        float center_w,
                        float top_y,
                        ImGuiWindowFlags panel_flags);

}  // namespace app
