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
    const ImWchar* jp_ranges = ImGui::GetIO().Fonts->GetGlyphRangesJapanese();

    HelloImGui::FontLoadingParams regular_params;
    regular_params.fontConfig.PixelSnapH = true;
    regular_params.fontConfig.GlyphRanges = jp_ranges;
    HelloImGui::LoadFont("fonts/NotoSansJP-Regular.otf", kFontSize, regular_params);

    HelloImGui::FontLoadingParams bold_params;
    bold_params.fontConfig.PixelSnapH = true;
    bold_params.fontConfig.GlyphRanges = jp_ranges;
    ui::gsn::g_BoldFont = HelloImGui::LoadFont("fonts/NotoSansJP-Bold.otf", kFontSize, bold_params);
}

}  // namespace app
