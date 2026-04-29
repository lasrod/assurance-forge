#include "ui/theme.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

constexpr ImU32 RGB(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

Theme MakeFallbackTheme() {
    Theme t{};

    // Surfaces
    t.bg_app        = RGB(0x0E, 0x11, 0x16);
    t.surface_1     = RGB(0x15, 0x19, 0x21);
    t.surface_2     = RGB(0x1C, 0x21, 0x2B);
    t.surface_3     = RGB(0x24, 0x2A, 0x36);

    // Borders
    t.border        = RGB(0x2A, 0x31, 0x40);
    t.border_strong = RGB(0x37, 0x40, 0x55);

    // Text
    t.text_primary   = RGB(0xE6, 0xEA, 0xF2);
    t.text_secondary = RGB(0x9A, 0xA3, 0xB2);
    t.text_muted     = RGB(0x6B, 0x73, 0x84);
    t.ink_dark       = RGB(0x1A, 0x1F, 0x2A);
    t.ink_darker     = RGB(0x0E, 0x11, 0x16);

    // Accent / semantic
    t.accent         = RGB(0x7C, 0x8C, 0xFF);
    t.accent_hover   = RGB(0x94, 0xA1, 0xFF);
    t.accent_pressed = RGB(0x66, 0x77, 0xF0);
    t.success        = RGB(0x4A, 0xDE, 0x80);
    t.warning        = RGB(0xF5, 0xB4, 0x54);
    t.danger         = RGB(0xEF, 0x6B, 0x6B);

    // GSN nodes (desaturated for dark canvas)
    t.node_claim         = RGB(0x5F, 0xB9, 0x7A);
    t.node_strategy      = RGB(0x6E, 0xA8, 0xE5);
    t.node_solution      = RGB(0xE0, 0xA2, 0x4A);
    t.node_context       = RGB(0xB7, 0xBE, 0xC9);
    t.node_assumption    = RGB(0xE6, 0x8B, 0x8B);
    t.node_justification = RGB(0x9F, 0xB6, 0xE2);
    t.node_evidence      = RGB(0xE0, 0xA2, 0x4A);

    // Edges
    t.edge_group1 = WithAlpha(t.text_secondary, 0.85f);
    t.edge_group2 = WithAlpha(t.accent, 0.70f);

    // Geometry
    t.rounding_ui       = 6.0f;
    t.rounding_panel    = 8.0f;
    t.rounding_node     = 8.0f;
    t.outline_thickness = 1.0f;
    t.shadow_alpha_top  = 0.55f;
    t.shadow_offset     = 4.0f;

    // Canvas
    t.canvas_bg          = t.bg_app;
    t.canvas_grid_minor  = WithAlpha(t.border, 0.55f);
    t.canvas_grid_major  = WithAlpha(t.border_strong, 0.65f);
    t.canvas_grid_spacing = 40.0f;

    return t;
}

ImVec4 ToVec4(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

ImU32 ToU32(ImVec4 v) {
    return ImGui::ColorConvertFloat4ToU32(v);
}

Theme MakeThemeFromStyle() {
    Theme t = MakeFallbackTheme();
    if (ImGui::GetCurrentContext() == nullptr) return t;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4* colors = style.Colors;

    t.bg_app = ToU32(colors[ImGuiCol_WindowBg]);
    t.surface_1 = ToU32(colors[ImGuiCol_WindowBg]);
    t.surface_2 = ToU32(colors[ImGuiCol_ChildBg]);
    if (((t.surface_2 >> IM_COL32_A_SHIFT) & 0xFF) == 0) {
        t.surface_2 = ToU32(colors[ImGuiCol_FrameBg]);
    }
    t.surface_3 = ToU32(colors[ImGuiCol_FrameBgHovered]);

    t.border = ToU32(colors[ImGuiCol_Border]);
    t.border_strong = ToU32(colors[ImGuiCol_SeparatorHovered]);

    t.text_primary = ToU32(colors[ImGuiCol_Text]);
    t.text_secondary = ToU32(colors[ImGuiCol_TextDisabled]);
    t.text_muted = WithAlpha(t.text_secondary, 0.72f);

    t.accent = ToU32(colors[ImGuiCol_CheckMark]);
    t.accent_hover = ToU32(colors[ImGuiCol_HeaderHovered]);
    t.accent_pressed = ToU32(colors[ImGuiCol_ButtonActive]);

    t.edge_group1 = WithAlpha(t.text_secondary, 0.88f);
    t.edge_group2 = WithAlpha(t.accent, 0.72f);

    t.rounding_ui = style.FrameRounding;
    t.rounding_panel = style.WindowRounding;
    t.canvas_bg = ShadeColor(t.bg_app, -0.08f);
    t.canvas_grid_minor = WithAlpha(t.border, 0.55f);
    t.canvas_grid_major = WithAlpha(t.border_strong, 0.65f);

    return t;
}

}  // namespace

const Theme& GetTheme() {
    static Theme theme;
    static bool initialized = false;
    static int cached_frame = -1;
    const bool has_context = (ImGui::GetCurrentContext() != nullptr);
    const int current_frame = has_context ? ImGui::GetFrameCount() : -1;

    if (!initialized || current_frame != cached_frame) {
        theme = has_context ? MakeThemeFromStyle() : MakeFallbackTheme();
        cached_frame = current_frame;
        initialized = true;
    }
    return theme;
}

ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    ImVec4 va = ToVec4(a);
    ImVec4 vb = ToVec4(b);
    ImVec4 r(va.x + (vb.x - va.x) * t,
             va.y + (vb.y - va.y) * t,
             va.z + (vb.z - va.z) * t,
             va.w + (vb.w - va.w) * t);
    return ToU32(r);
}

ImU32 WithAlpha(ImU32 c, float factor) {
    int a = (int)((c >> IM_COL32_A_SHIFT) & 0xFF);
    int new_a = (int)std::round((float)a * factor);
    if (new_a < 0) new_a = 0;
    if (new_a > 255) new_a = 255;
    return (c & ~IM_COL32_A_MASK) | ((ImU32)new_a << IM_COL32_A_SHIFT);
}

ImU32 ShadeColor(ImU32 c, float amount) {
    ImVec4 v = ToVec4(c);
    if (amount >= 0.0f) {
        v.x = v.x + (1.0f - v.x) * amount;
        v.y = v.y + (1.0f - v.y) * amount;
        v.z = v.z + (1.0f - v.z) * amount;
    } else {
        float k = 1.0f + amount;  // amount is negative
        v.x *= k;
        v.y *= k;
        v.z *= k;
    }
    return ToU32(v);
}

ImU32 InkOn(ImU32 background) {
    ImVec4 v = ToVec4(background);
    // Perceived luminance (Rec. 601 weights)
    float lum = 0.299f * v.x + 0.587f * v.y + 0.114f * v.z;
    const Theme& theme = GetTheme();
    return (lum > 0.55f) ? theme.ink_dark : theme.text_primary;
}

}  // namespace ui
