// Assurance Forge application bootstrap.
// Platform details are intentionally delegated to app/platform_win32_dx11.cpp.

#include "app/app_runtime.h"
#include "app/platform_win32_dx11.h"

#include "imgui.h"

#include "ui/gsn_canvas.h"
#include "ui/theme.h"

#include <cstdio>

namespace {

void MergeJapaneseGlyphs(ImGuiIO& io, float font_size, const ImFontConfig& base_cfg) {
    ImFontConfig cfg = base_cfg;
    const char* jp_fonts[] = {
        "C:\\Windows\\Fonts\\YuGothR.ttc",
        "C:\\Windows\\Fonts\\msgothic.ttc",
        "C:\\Windows\\Fonts\\meiryo.ttc",
        nullptr
    };

    for (const char** jp = jp_fonts; *jp; ++jp) {
        FILE* f = fopen(*jp, "rb");
        if (!f) continue;
        fclose(f);
        io.Fonts->AddFontFromFileTTF(*jp, font_size, &cfg, io.Fonts->GetGlyphRangesJapanese());
        break;
    }
}

void ConfigureImGuiFonts() {
    ImGuiIO& io = ImGui::GetIO();
    constexpr float kFontSize = 15.0f;

    const char* regular_font = "C:\\Windows\\Fonts\\segoeui.ttf";
    const char* bold_font    = "C:\\Windows\\Fonts\\segoeuib.ttf";

    ImFontConfig base_cfg;
    base_cfg.PixelSnapH = true;

    // Default UI font (regular, 15)
    io.Fonts->AddFontFromFileTTF(regular_font, kFontSize, &base_cfg);

    ImFontConfig merge_cfg;
    merge_cfg.MergeMode  = true;
    merge_cfg.PixelSnapH = true;
    MergeJapaneseGlyphs(io, kFontSize, merge_cfg);

    // Bold (15) used for node label first lines and panel headers
    ui::g_BoldFont = io.Fonts->AddFontFromFileTTF(bold_font, kFontSize, &base_cfg);
    if (ui::g_BoldFont) {
        MergeJapaneseGlyphs(io, kFontSize, merge_cfg);
    } else {
        ui::g_BoldFont = io.Fonts->Fonts[0];
    }
}

}  // namespace

int WINAPI WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance, LPSTR lp_cmd_line, int n_cmd_show) {
    (void)h_prev_instance;
    (void)lp_cmd_line;

    app::platform::Win32Dx11Context platform_ctx;
    if (!app::platform::Initialize(h_instance, n_cmd_show, platform_ctx)) {
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ui::ApplyImGuiStyle();
    ConfigureImGuiFonts();

    if (!app::platform::InitializeImGuiBackends(platform_ctx)) {
        ImGui::DestroyContext();
        app::platform::Shutdown(platform_ctx);
        return 1;
    }

    app::AppRuntime runtime;

    // Framebuffer clear color: matches the theme's app-background tier so
    // panel rounding has a consistent surround.
    const ImVec4 bg_v = ImGui::ColorConvertU32ToFloat4(ui::GetTheme().bg_app);
    const float clear_color[4] = { bg_v.x, bg_v.y, bg_v.z, 1.0f };

    bool done = false;
    while (!done) {
        if (!app::platform::PollEvents(done)) {
            break;
        }

        app::platform::BeginFrame();
        runtime.RenderFrame(done);
        app::platform::RenderFrame(platform_ctx, clear_color);
    }

    app::platform::ShutdownImGuiBackends();
    ImGui::DestroyContext();
    app::platform::Shutdown(platform_ctx);

    return 0;
}
// Assurance Forge application bootstrap.
// Platform details are intentionally delegated to app/platform_win32_dx11.cpp.
