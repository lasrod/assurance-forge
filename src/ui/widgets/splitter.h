#pragma once

#include "imgui.h"

namespace ui::widgets {

void DrawVerticalSplitter(const char* id,
                          float x,
                          float width,
                          float height,
                          float top_y,
                          float display_w,
                          float& ratio,
                          bool subtract_delta,
                          float min_ratio,
                          float max_ratio,
                          float hit_padding,
                          ImGuiWindowFlags panel_flags);

float DrawHorizontalSplitter(
    const char* id, float x, float y, float width, float height, float hit_padding, ImGuiWindowFlags panel_flags);

} // namespace ui::widgets
