#include "ui/theme.h"

#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/imgui_theme.h"
#ifdef _WIN32
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace ui {

namespace {

constexpr ImU32 RGB(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

ImVec4 Color(int r, int g, int b, int a = 255) {
    return ImVec4(static_cast<float>(r) / 255.0f,
                  static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f,
                  static_cast<float>(a) / 255.0f);
}

AppTheme g_current_app_theme = AppTheme::Dark;

ImGuiTheme::ImGuiTheme_ BaseImGuiTheme(AppTheme theme) {
    return theme == AppTheme::Light ? ImGuiTheme::ImGuiTheme_ImGuiColorsLight : ImGuiTheme::ImGuiTheme_ImGuiColorsDark;
}

#ifdef _WIN32
void ApplyWin32WindowTheme(AppTheme theme, HelloImGui::RunnerParams* runner_params) {
    if (runner_params == nullptr || runner_params->backendPointers.glfwWindow == nullptr)
        return;

    auto* glfw_window = static_cast<GLFWwindow*>(runner_params->backendPointers.glfwWindow);
    HWND hwnd = glfwGetWin32Window(glfw_window);
    if (hwnd == nullptr)
        return;

    const bool use_dark_theme = theme == AppTheme::Dark;
    BOOL use_dark_mode = use_dark_theme ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &use_dark_mode, sizeof(use_dark_mode));

    COLORREF caption_color = use_dark_theme ? RGB(10, 15, 24) : RGB(251, 252, 253);
    COLORREF text_color = use_dark_theme ? RGB(230, 235, 245) : RGB(23, 28, 34);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption_color, sizeof(caption_color));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text_color, sizeof(text_color));
}
#endif

std::string NormalizeThemeName(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c))
            normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    return normalized;
}

void ApplySharedStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 11.0f;

    style.TabBarBorderSize = 0.0f;
    style.TabBarOverlineSize = 0.0f;
    style.TabCloseButtonMinWidthUnselected = 0.0f;
    style.SeparatorTextBorderSize = 0.0f;
    style.DockingSeparatorSize = 1.0f;
}

void ApplyDarkColors() {
    ImVec4* colors = ImGui::GetStyle().Colors;

    const ImVec4 bg = Color(0x0E, 0x12, 0x18);
    const ImVec4 panel = Color(0x13, 0x18, 0x20);
    const ImVec4 panel_hover = Color(0x1F, 0x26, 0x32);
    const ImVec4 control = Color(0x1B, 0x23, 0x2F);
    const ImVec4 control_hover = Color(0x25, 0x32, 0x44);
    const ImVec4 control_active = Color(0x33, 0x4D, 0x6C);
    const ImVec4 accent = Color(0x42, 0x85, 0xCC);
    const ImVec4 accent_hover = Color(0x5B, 0x9B, 0xDD);
    const ImVec4 text = Color(0xE6, 0xEA, 0xF2);
    const ImVec4 text_muted = Color(0x94, 0xA6, 0xB8);
    const ImVec4 border = Color(0x2A, 0x38, 0x4A, 0x8A);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = text_muted;
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = Color(0x16, 0x1C, 0x26);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = Color(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = control;
    colors[ImGuiCol_FrameBgHovered] = control_hover;
    colors[ImGuiCol_FrameBgActive] = control_active;
    colors[ImGuiCol_TitleBg] = bg;
    colors[ImGuiCol_TitleBgActive] = panel;
    colors[ImGuiCol_TitleBgCollapsed] = bg;
    colors[ImGuiCol_MenuBarBg] = Color(0x11, 0x16, 0x1E);
    colors[ImGuiCol_ScrollbarBg] = Color(0x0B, 0x0F, 0x15);
    colors[ImGuiCol_ScrollbarGrab] = Color(0x2A, 0x36, 0x46);
    colors[ImGuiCol_ScrollbarGrabHovered] = Color(0x36, 0x46, 0x5A);
    colors[ImGuiCol_ScrollbarGrabActive] = Color(0x48, 0x5E, 0x76);
    colors[ImGuiCol_CheckMark] = accent_hover;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_hover;
    colors[ImGuiCol_Button] = control;
    colors[ImGuiCol_ButtonHovered] = control_hover;
    colors[ImGuiCol_ButtonActive] = control_active;
    colors[ImGuiCol_Header] = Color(0x25, 0x39, 0x52, 0xC8);
    colors[ImGuiCol_HeaderHovered] = Color(0x30, 0x4C, 0x70, 0xDD);
    colors[ImGuiCol_HeaderActive] = Color(0x3A, 0x5F, 0x8F, 0xEE);
    colors[ImGuiCol_Separator] = Color(0x2A, 0x38, 0x4A, 0x70);
    colors[ImGuiCol_SeparatorHovered] = Color(0x42, 0x85, 0xCC, 0xAA);
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = Color(0x42, 0x85, 0xCC, 0x36);
    colors[ImGuiCol_ResizeGripHovered] = Color(0x42, 0x85, 0xCC, 0x72);
    colors[ImGuiCol_ResizeGripActive] = Color(0x5B, 0x9B, 0xDD, 0xA8);
    colors[ImGuiCol_Tab] = panel;
    colors[ImGuiCol_TabHovered] = control_hover;
    colors[ImGuiCol_TabSelected] = panel_hover;
    colors[ImGuiCol_TabSelectedOverline] = Color(0x42, 0x85, 0xCC, 0x00);
    colors[ImGuiCol_TabDimmed] = Color(0x0F, 0x14, 0x1C);
    colors[ImGuiCol_TabDimmedSelected] = Color(0x19, 0x20, 0x2B);
    colors[ImGuiCol_TabDimmedSelectedOverline] = Color(0x42, 0x85, 0xCC, 0x00);
    colors[ImGuiCol_DockingPreview] = Color(0x42, 0x85, 0xCC, 0x66);
    colors[ImGuiCol_DockingEmptyBg] = panel;
    colors[ImGuiCol_TableHeaderBg] = Color(0x18, 0x20, 0x2B);
    colors[ImGuiCol_TableBorderStrong] = Color(0x33, 0x42, 0x56, 0x90);
    colors[ImGuiCol_TableBorderLight] = Color(0x2A, 0x38, 0x4A, 0x58);
    colors[ImGuiCol_TableRowBg] = Color(0x00, 0x00, 0x00, 0x00);
    colors[ImGuiCol_TableRowBgAlt] = Color(0xFF, 0xFF, 0xFF, 0x08);
    colors[ImGuiCol_TextSelectedBg] = Color(0x42, 0x85, 0xCC, 0x58);
    colors[ImGuiCol_DragDropTarget] = Color(0x5B, 0x9B, 0xDD, 0xCC);
    colors[ImGuiCol_NavHighlight] = Color(0x5B, 0x9B, 0xDD, 0xB8);
    colors[ImGuiCol_ModalWindowDimBg] = Color(0x04, 0x08, 0x0D, 0xAA);
}

void ApplyLightColors() {
    ImVec4* colors = ImGui::GetStyle().Colors;

    const ImVec4 bg = Color(0xFB, 0xFC, 0xFD);
    const ImVec4 panel = Color(0xF2, 0xF5, 0xF9);
    const ImVec4 panel_hover = Color(0xE6, 0xE9, 0xF0);
    const ImVec4 control = Color(0xE9, 0xEE, 0xF5);
    const ImVec4 control_hover = Color(0xD8, 0xE5, 0xF5);
    const ImVec4 control_active = Color(0xB9, 0xD0, 0xEC);
    const ImVec4 accent = Color(0x29, 0x6B, 0xB8);
    const ImVec4 accent_hover = Color(0x3C, 0x7E, 0xCB);
    const ImVec4 text = Color(0x17, 0x1C, 0x22);
    const ImVec4 text_muted = Color(0x6B, 0x75, 0x85);
    const ImVec4 border = Color(0xD9, 0xE1, 0xEC, 0xB8);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = text_muted;
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = Color(0xFF, 0xFF, 0xFF);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = Color(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = control;
    colors[ImGuiCol_FrameBgHovered] = control_hover;
    colors[ImGuiCol_FrameBgActive] = control_active;
    colors[ImGuiCol_TitleBg] = panel;
    colors[ImGuiCol_TitleBgActive] = panel;
    colors[ImGuiCol_TitleBgCollapsed] = panel;
    colors[ImGuiCol_MenuBarBg] = Color(0xF6, 0xF8, 0xFB);
    colors[ImGuiCol_ScrollbarBg] = Color(0xF2, 0xF5, 0xF9);
    colors[ImGuiCol_ScrollbarGrab] = Color(0xC5, 0xCF, 0xDC);
    colors[ImGuiCol_ScrollbarGrabHovered] = Color(0xAC, 0xBA, 0xCA);
    colors[ImGuiCol_ScrollbarGrabActive] = Color(0x93, 0xA5, 0xB8);
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_hover;
    colors[ImGuiCol_Button] = control;
    colors[ImGuiCol_ButtonHovered] = control_hover;
    colors[ImGuiCol_ButtonActive] = control_active;
    colors[ImGuiCol_Header] = Color(0xD8, 0xE8, 0xFA, 0xD8);
    colors[ImGuiCol_HeaderHovered] = Color(0xC8, 0xDD, 0xF6, 0xE6);
    colors[ImGuiCol_HeaderActive] = Color(0xB9, 0xD0, 0xEC, 0xF0);
    colors[ImGuiCol_Separator] = Color(0xD9, 0xE1, 0xEC, 0xA0);
    colors[ImGuiCol_SeparatorHovered] = Color(0x29, 0x6B, 0xB8, 0x88);
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = Color(0x29, 0x6B, 0xB8, 0x28);
    colors[ImGuiCol_ResizeGripHovered] = Color(0x29, 0x6B, 0xB8, 0x5E);
    colors[ImGuiCol_ResizeGripActive] = Color(0x29, 0x6B, 0xB8, 0x90);
    colors[ImGuiCol_Tab] = panel;
    colors[ImGuiCol_TabHovered] = Color(0xC8, 0xDD, 0xF6);
    colors[ImGuiCol_TabSelected] = Color(0xD6, 0xE8, 0xFB);
    colors[ImGuiCol_TabSelectedOverline] = Color(0x29, 0x6B, 0xB8, 0xCC);
    colors[ImGuiCol_TabDimmed] = Color(0xEA, 0xEF, 0xF5);
    colors[ImGuiCol_TabDimmedSelected] = Color(0xE2, 0xEC, 0xF8);
    colors[ImGuiCol_TabDimmedSelectedOverline] = Color(0x29, 0x6B, 0xB8, 0x99);
    colors[ImGuiCol_DockingPreview] = Color(0x29, 0x6B, 0xB8, 0x55);
    colors[ImGuiCol_DockingEmptyBg] = panel;
    colors[ImGuiCol_TableHeaderBg] = Color(0xEC, 0xF1, 0xF7);
    colors[ImGuiCol_TableBorderStrong] = Color(0xC9, 0xD3, 0xE0, 0xB8);
    colors[ImGuiCol_TableBorderLight] = Color(0xDF, 0xE6, 0xEF, 0xA0);
    colors[ImGuiCol_TableRowBg] = Color(0x00, 0x00, 0x00, 0x00);
    colors[ImGuiCol_TableRowBgAlt] = Color(0x29, 0x6B, 0xB8, 0x07);
    colors[ImGuiCol_TextSelectedBg] = Color(0x96, 0xBE, 0xF0, 0x88);
    colors[ImGuiCol_DragDropTarget] = Color(0x29, 0x6B, 0xB8, 0xBB);
    colors[ImGuiCol_NavHighlight] = Color(0x29, 0x6B, 0xB8, 0x9A);
    colors[ImGuiCol_ModalWindowDimBg] = Color(0x17, 0x1C, 0x22, 0x40);
}

Theme MakeTheme(AppTheme app_theme) {
    Theme t{};

    if (app_theme == AppTheme::Light) {
        t.bg_app = RGB(0xFB, 0xFC, 0xFD);
        t.surface_1 = RGB(0xF2, 0xF5, 0xF9);
        t.surface_2 = RGB(0xE6, 0xE9, 0xF0);
        t.surface_3 = RGB(0xD8, 0xE5, 0xF5);

        t.border = RGB(0xD9, 0xE1, 0xEC);
        t.border_strong = RGB(0xB7, 0xC4, 0xD3);

        t.text_primary = RGB(0x17, 0x1C, 0x22);
        t.text_secondary = RGB(0x5D, 0x68, 0x78);
        t.text_muted = RGB(0x7C, 0x86, 0x96);
        t.ink_dark = RGB(0x17, 0x1C, 0x22);
        t.ink_darker = RGB(0x0F, 0x14, 0x1A);

        t.accent = RGB(0x29, 0x6B, 0xB8);
        t.accent_hover = RGB(0x3C, 0x7E, 0xCB);
        t.accent_pressed = RGB(0x1F, 0x58, 0x9D);
        t.success = RGB(0x2F, 0x8A, 0x52);
        t.warning = RGB(0xB8, 0x72, 0x18);
        t.danger = RGB(0xC8, 0x3E, 0x3E);
        t.info = RGB(0x3B, 0x72, 0xB2);
        t.attention = RGB(0xB8, 0x72, 0x18);

        t.node_claim = RGB(0xB8, 0xDB, 0xC3);
        t.node_strategy = RGB(0xC7, 0xD9, 0xF2);
        t.node_solution = RGB(0xF2, 0xDC, 0xB4);
        t.node_context = RGB(0xE1, 0xE6, 0xED);
        t.node_assumption = RGB(0xF2, 0xC6, 0xC6);
        t.node_justification = RGB(0xD1, 0xDA, 0xEF);
        t.node_evidence = RGB(0xF2, 0xDC, 0xB4);

        t.node_claim_text = RGB(0x2F, 0x75, 0x46);
        t.node_strategy_text = RGB(0x38, 0x66, 0x98);
        t.node_solution_text = RGB(0x9A, 0x62, 0x17);
        t.node_context_text = RGB(0x5A, 0x66, 0x76);
        t.node_assumption_text = RGB(0xA9, 0x43, 0x43);
        t.node_justification_text = RGB(0x5A, 0x68, 0x94);
        t.node_evidence_text = t.node_solution_text;

        t.edge_group1 = WithAlpha(t.text_secondary, 0.82f);
        t.edge_group2 = WithAlpha(t.accent, 0.68f);

        t.canvas_bg = RGB(0xFF, 0xFF, 0xFF);
        t.canvas_grid_minor = WithAlpha(t.border, 0.70f);
        t.canvas_grid_major = WithAlpha(t.border_strong, 0.65f);
    } else {
        t.bg_app = RGB(0x0E, 0x12, 0x18);
        t.surface_1 = RGB(0x13, 0x18, 0x20);
        t.surface_2 = RGB(0x1B, 0x23, 0x2F);
        t.surface_3 = RGB(0x25, 0x32, 0x44);

        t.border = RGB(0x2A, 0x38, 0x4A);
        t.border_strong = RGB(0x3A, 0x4D, 0x66);

        t.text_primary = RGB(0xE6, 0xEA, 0xF2);
        t.text_secondary = RGB(0x94, 0xA6, 0xB8);
        t.text_muted = RGB(0x6D, 0x7D, 0x8F);
        t.ink_dark = RGB(0x1A, 0x1F, 0x2A);
        t.ink_darker = RGB(0x0E, 0x11, 0x16);

        t.accent = RGB(0x42, 0x85, 0xCC);
        t.accent_hover = RGB(0x5B, 0x9B, 0xDD);
        t.accent_pressed = RGB(0x31, 0x67, 0xA3);
        t.success = RGB(0x59, 0xC2, 0x7A);
        t.warning = RGB(0xE3, 0xA5, 0x43);
        t.danger = RGB(0xE8, 0x66, 0x66);
        t.info = RGB(0x6F, 0xA8, 0xE5);
        t.attention = RGB(0xE3, 0xA5, 0x43);

        t.node_claim = RGB(0x5F, 0xB9, 0x7A);
        t.node_strategy = RGB(0x6E, 0xA8, 0xE5);
        t.node_solution = RGB(0xE0, 0xA2, 0x4A);
        t.node_context = RGB(0xB7, 0xBE, 0xC9);
        t.node_assumption = RGB(0xE6, 0x8B, 0x8B);
        t.node_justification = RGB(0x9F, 0xB6, 0xE2);
        t.node_evidence = RGB(0xE0, 0xA2, 0x4A);

        t.node_claim_text = ShadeColor(t.node_claim, 0.25f);
        t.node_strategy_text = ShadeColor(t.node_strategy, 0.25f);
        t.node_solution_text = ShadeColor(t.node_solution, 0.25f);
        t.node_context_text = ShadeColor(t.node_context, 0.25f);
        t.node_assumption_text = ShadeColor(t.node_assumption, 0.25f);
        t.node_justification_text = ShadeColor(t.node_justification, 0.25f);
        t.node_evidence_text = ShadeColor(t.node_evidence, 0.25f);

        t.edge_group1 = WithAlpha(t.text_secondary, 0.85f);
        t.edge_group2 = WithAlpha(t.accent, 0.70f);

        t.canvas_bg = ShadeColor(t.bg_app, -0.08f);
        t.canvas_grid_minor = WithAlpha(t.border, 0.55f);
        t.canvas_grid_major = WithAlpha(t.border_strong, 0.65f);
    }

    t.rounding_ui = 6.0f;
    t.rounding_panel = 8.0f;
    t.rounding_node = 8.0f;
    t.outline_thickness = 1.0f;
    t.shadow_alpha_top = 0.55f;
    t.shadow_offset = 4.0f;

    t.canvas_grid_spacing = 40.0f;

    return t;
}

ImVec4 ToVec4(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

ImU32 ToU32(ImVec4 v) {
    return ImGui::ColorConvertFloat4ToU32(v);
}

} // namespace

void ApplyAppTheme(AppTheme theme) {
    g_current_app_theme = theme;

    const ImGuiTheme::ImGuiTheme_ base_theme = BaseImGuiTheme(theme);
    if (HelloImGui::IsUsingHelloImGui()) {
        HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
        if (runner_params != nullptr) {
            runner_params->imGuiWindowParams.tweakedTheme = ImGuiTheme::ImGuiTweakedTheme(base_theme);
#ifdef _WIN32
            ApplyWin32WindowTheme(theme, runner_params);
#endif
        }
    }

    if (ImGui::GetCurrentContext() == nullptr)
        return;

    if (HelloImGui::IsUsingHelloImGui()) {
        ImGuiTheme::ApplyTheme(base_theme);
    } else {
        ImGui::GetStyle() = ImGuiTheme::ThemeToStyle(base_theme);
    }
    ApplySharedStyle();
    if (theme == AppTheme::Light) {
        ApplyLightColors();
    } else {
        ApplyDarkColors();
    }
    if (HelloImGui::IsUsingHelloImGui()) {
        HelloImGui::RunnerParams* runner_params = HelloImGui::GetRunnerParams();
        if (runner_params != nullptr) {
            // Ensure the framebuffer clear color matches panel background so
            // rounded panel corners blend instead of revealing a dark underlay.
            runner_params->imGuiWindowParams.backgroundColor = ImGui::GetStyle().Colors[ImGuiCol_DockingEmptyBg];
        }
    }
}

AppTheme GetCurrentAppTheme() {
    return g_current_app_theme;
}

AppTheme GetDefaultAppTheme() {
    return AppTheme::Dark;
}

const char* GetThemeDisplayName(AppTheme theme) {
    switch (theme) {
    case AppTheme::Dark:
        return "Dark";
    case AppTheme::Light:
        return "Light";
    }
    return "Dark";
}

AppTheme ParseAppTheme(std::string_view value) {
    const std::string normalized = NormalizeThemeName(value);
    if (normalized == "light" || normalized == "forgelight" || normalized == "bright" ||
        normalized == "imguicolorslight" || normalized == "lightrounded" || normalized == "whiteiswhite") {
        return AppTheme::Light;
    }
    return AppTheme::Dark;
}

std::string_view SerializeAppTheme(AppTheme theme) {
    return theme == AppTheme::Light ? std::string_view("Light") : std::string_view("Dark");
}

const Theme& GetTheme() {
    static Theme theme;
    static bool initialized = false;
    static int cached_frame = -1;
    static AppTheme cached_app_theme = AppTheme::Dark;
    const bool has_context = (ImGui::GetCurrentContext() != nullptr);
    const int current_frame = has_context ? ImGui::GetFrameCount() : -1;

    if (!initialized || current_frame != cached_frame || cached_app_theme != g_current_app_theme) {
        theme = MakeTheme(g_current_app_theme);
        cached_frame = current_frame;
        cached_app_theme = g_current_app_theme;
        initialized = true;
    }
    return theme;
}

ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;
    ImVec4 va = ToVec4(a);
    ImVec4 vb = ToVec4(b);
    ImVec4 r(va.x + (vb.x - va.x) * t, va.y + (vb.y - va.y) * t, va.z + (vb.z - va.z) * t, va.w + (vb.w - va.w) * t);
    return ToU32(r);
}

ImU32 WithAlpha(ImU32 c, float factor) {
    int a = (int)((c >> IM_COL32_A_SHIFT) & 0xFF);
    int new_a = (int)std::round((float)a * factor);
    if (new_a < 0)
        new_a = 0;
    if (new_a > 255)
        new_a = 255;
    return (c & ~IM_COL32_A_MASK) | ((ImU32)new_a << IM_COL32_A_SHIFT);
}

ImU32 ShadeColor(ImU32 c, float amount) {
    ImVec4 v = ToVec4(c);
    if (amount >= 0.0f) {
        v.x = v.x + (1.0f - v.x) * amount;
        v.y = v.y + (1.0f - v.y) * amount;
        v.z = v.z + (1.0f - v.z) * amount;
    } else {
        float k = 1.0f + amount; // amount is negative
        v.x *= k;
        v.y *= k;
        v.z *= k;
    }
    return ToU32(v);
}

ImVec4 CullRatioColor(float ratio) {
    const Theme& theme = GetTheme();
    if (ratio >= 0.70f)
        return ImGui::ColorConvertU32ToFloat4(theme.success);
    if (ratio >= 0.35f)
        return ImGui::ColorConvertU32ToFloat4(theme.warning);
    return ImGui::ColorConvertU32ToFloat4(theme.text_secondary);
}

ImVec4 GetSuccessColor() {
    return ImGui::ColorConvertU32ToFloat4(GetTheme().success);
}

ImVec4 GetWarningColor() {
    return ImGui::ColorConvertU32ToFloat4(GetTheme().warning);
}

ImVec4 GetErrorColor() {
    return ImGui::ColorConvertU32ToFloat4(GetTheme().danger);
}

ImVec4 GetInfoColor() {
    return ImGui::ColorConvertU32ToFloat4(GetTheme().info);
}

ImVec4 GetMutedTextColor() {
    return ImGui::ColorConvertU32ToFloat4(GetTheme().text_muted);
}

ImVec4 GetAttentionColor() {
    return ImGui::ColorConvertU32ToFloat4(GetTheme().attention);
}

ImU32 InkOn(ImU32 background) {
    ImVec4 v = ToVec4(background);
    // Perceived luminance (Rec. 601 weights)
    float lum = 0.299f * v.x + 0.587f * v.y + 0.114f * v.z;
    const Theme& theme = GetTheme();
    return (lum > 0.55f) ? theme.ink_dark : theme.text_primary;
}

} // namespace ui
