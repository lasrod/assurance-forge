#include "app/app_ui_bootstrap.h"

#include "hello_imgui/hello_imgui.h"
#include "imgui.h"

#include "ui/gsn/gsn_canvas.h"
#include "ui/theme.h"

namespace app {

void ConfigureImGuiConfig() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
}

void ConfigureImGuiStyle() {
    ImGui::StyleColorsDark();
    ui::ApplyImGuiStyle();
}

void ConfigureImGuiFonts() {
    constexpr float kFontSize = 15.0f;
    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* jp_ranges = io.Fonts->GetGlyphRangesJapanese();

    HelloImGui::FontLoadingParams regular_params;
    regular_params.fontConfig.PixelSnapH = true;
    regular_params.fontConfig.GlyphRanges = jp_ranges;
    ImFont* regular_font = HelloImGui::LoadFont("fonts/NotoSansJP-Regular.otf", kFontSize, regular_params);

    HelloImGui::FontLoadingParams bold_params;
    bold_params.fontConfig.PixelSnapH = true;
    bold_params.fontConfig.GlyphRanges = jp_ranges;
    ImFont* bold_font = HelloImGui::LoadFont("fonts/NotoSansJP-Bold.otf", kFontSize, bold_params);

    ui::gsn::g_BoldFont = bold_font;
    if (ui::gsn::g_BoldFont == nullptr) {
        if (regular_font != nullptr) {
            ui::gsn::g_BoldFont = regular_font;
        } else if (!io.Fonts->Fonts.empty()) {
            ui::gsn::g_BoldFont = io.Fonts->Fonts[0];
        } else {
            ui::gsn::g_BoldFont = ImGui::GetFont();
        }
    }
}

}  // namespace app
