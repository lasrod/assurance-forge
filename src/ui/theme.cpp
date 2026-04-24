#include "ui/theme.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

constexpr ImU32 RGB(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

Theme MakeAuroraDark() {
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

const Theme& Aurora() {
    static const Theme t = MakeAuroraDark();
    return t;
}

ImVec4 ToVec4(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

ImU32 ToU32(ImVec4 v) {
    return ImGui::ColorConvertFloat4ToU32(v);
}

}  // namespace

const Theme& GetTheme() {
    return Aurora();
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
    return (lum > 0.55f) ? Aurora().ink_dark : Aurora().text_primary;
}

void ApplyImGuiStyle() {
    const Theme& t = Aurora();
    ImGuiStyle& s = ImGui::GetStyle();

    // Geometry
    s.WindowRounding    = t.rounding_panel;
    s.ChildRounding     = t.rounding_panel;
    s.PopupRounding     = t.rounding_panel;
    s.FrameRounding     = t.rounding_ui;
    s.GrabRounding      = t.rounding_ui;
    s.ScrollbarRounding = t.rounding_ui + 2.0f;
    s.TabRounding       = t.rounding_ui;

    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
    s.TabBorderSize     = 0.0f;

    s.WindowPadding     = ImVec2(12.0f, 10.0f);
    s.FramePadding      = ImVec2(10.0f, 6.0f);
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);
    s.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 14.0f;
    s.SeparatorTextBorderSize = 1.0f;

    s.AntiAliasedLines       = true;
    s.AntiAliasedLinesUseTex = true;
    s.AntiAliasedFill        = true;

    // Colors
    auto V = [](ImU32 c) { return ToVec4(c); };

    s.Colors[ImGuiCol_WindowBg]            = V(t.surface_1);
    s.Colors[ImGuiCol_ChildBg]             = V(t.surface_1);
    s.Colors[ImGuiCol_PopupBg]             = V(t.surface_2);
    s.Colors[ImGuiCol_MenuBarBg]           = V(t.surface_2);
    s.Colors[ImGuiCol_Border]              = V(WithAlpha(t.border, 0.85f));
    s.Colors[ImGuiCol_BorderShadow]        = ImVec4(0, 0, 0, 0);

    s.Colors[ImGuiCol_Text]                = V(t.text_primary);
    s.Colors[ImGuiCol_TextDisabled]        = V(t.text_muted);
    s.Colors[ImGuiCol_TextSelectedBg]      = V(WithAlpha(t.accent, 0.40f));

    s.Colors[ImGuiCol_FrameBg]             = V(t.surface_2);
    s.Colors[ImGuiCol_FrameBgHovered]      = V(t.surface_3);
    s.Colors[ImGuiCol_FrameBgActive]       = V(WithAlpha(t.accent, 0.30f));

    s.Colors[ImGuiCol_TitleBg]             = V(t.surface_2);
    s.Colors[ImGuiCol_TitleBgActive]       = V(t.surface_2);
    s.Colors[ImGuiCol_TitleBgCollapsed]    = V(WithAlpha(t.surface_2, 0.75f));

    s.Colors[ImGuiCol_Button]              = V(t.surface_3);
    s.Colors[ImGuiCol_ButtonHovered]       = V(LerpColor(t.surface_3, t.accent, 0.35f));
    s.Colors[ImGuiCol_ButtonActive]        = V(t.accent_pressed);

    s.Colors[ImGuiCol_Header]              = V(WithAlpha(t.accent, 0.20f));
    s.Colors[ImGuiCol_HeaderHovered]       = V(WithAlpha(t.accent, 0.32f));
    s.Colors[ImGuiCol_HeaderActive]        = V(WithAlpha(t.accent, 0.45f));

    s.Colors[ImGuiCol_Separator]           = V(t.border);
    s.Colors[ImGuiCol_SeparatorHovered]    = V(t.accent_hover);
    s.Colors[ImGuiCol_SeparatorActive]     = V(t.accent);

    s.Colors[ImGuiCol_ResizeGrip]          = V(WithAlpha(t.accent, 0.30f));
    s.Colors[ImGuiCol_ResizeGripHovered]   = V(WithAlpha(t.accent, 0.55f));
    s.Colors[ImGuiCol_ResizeGripActive]    = V(t.accent);

    s.Colors[ImGuiCol_Tab]                 = V(t.surface_1);
    s.Colors[ImGuiCol_TabHovered]          = V(LerpColor(t.surface_2, t.accent, 0.30f));
    s.Colors[ImGuiCol_TabActive]           = V(t.surface_2);
    s.Colors[ImGuiCol_TabUnfocused]        = V(t.surface_1);
    s.Colors[ImGuiCol_TabUnfocusedActive]  = V(t.surface_2);

    s.Colors[ImGuiCol_ScrollbarBg]         = V(WithAlpha(t.surface_1, 0.50f));
    s.Colors[ImGuiCol_ScrollbarGrab]       = V(t.surface_3);
    s.Colors[ImGuiCol_ScrollbarGrabHovered]= V(LerpColor(t.surface_3, t.accent, 0.40f));
    s.Colors[ImGuiCol_ScrollbarGrabActive] = V(t.accent);

    s.Colors[ImGuiCol_CheckMark]           = V(t.accent);
    s.Colors[ImGuiCol_SliderGrab]          = V(t.accent);
    s.Colors[ImGuiCol_SliderGrabActive]    = V(t.accent_hover);

    s.Colors[ImGuiCol_NavHighlight]        = V(t.accent_hover);
    s.Colors[ImGuiCol_NavWindowingHighlight] = V(WithAlpha(t.text_primary, 0.70f));

    s.Colors[ImGuiCol_TableHeaderBg]       = V(t.surface_2);
    s.Colors[ImGuiCol_TableBorderStrong]   = V(t.border_strong);
    s.Colors[ImGuiCol_TableBorderLight]    = V(t.border);
    s.Colors[ImGuiCol_TableRowBg]          = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_TableRowBgAlt]       = V(WithAlpha(t.surface_2, 0.40f));
}

}  // namespace ui
