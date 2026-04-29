#pragma once

#include "imgui.h"

namespace ui::gsn {

inline constexpr float kReferenceFontSize = 15.0f;

inline float DpiScale() {
    if (!ImGui::GetCurrentContext()) return 1.0f;
    float font_size = ImGui::GetFontSize();
    return font_size > 0.0f ? font_size / kReferenceFontSize : 1.0f;
}

inline float DpiSize(float reference_pixels) {
    return reference_pixels * DpiScale();
}

}  // namespace ui::gsn
