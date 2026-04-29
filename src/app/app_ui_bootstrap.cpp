#include "app/app_ui_bootstrap.h"

#include "hello_imgui/hello_imgui.h"
#include "imgui.h"

#include "ui/gsn/gsn_canvas.h"

namespace app {

void ConfigureImGuiConfig() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void ConfigureImGuiFonts() {
    constexpr float kFontSize = 15.0f;
    ImGuiIO& io = ImGui::GetIO();

    HelloImGui::FontLoadingParams regular_params;
    regular_params.fontConfig.PixelSnapH = true;
    ImFont* regular_font = HelloImGui::LoadFont("fonts/NotoSansJP-Regular.otf", kFontSize, regular_params);
    if (regular_font != nullptr) {
        io.FontDefault = regular_font;
    }

    HelloImGui::FontLoadingParams bold_params;
    bold_params.fontConfig.PixelSnapH = true;
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
