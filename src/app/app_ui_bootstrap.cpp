#include "app/app_ui_bootstrap.h"

#include "imgui.h"

#include "ui/gsn/gsn_canvas.h"
#include "ui/theme.h"

#include <cstdio>

namespace app {
namespace {

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
}

void MergeJapaneseGlyphs(ImGuiIO& io, float font_size, const ImFontConfig& base_cfg) {
    ImFontConfig cfg = base_cfg;
#ifdef _WIN32
    const char* jp_fonts[] = {
        "C:\\Windows\\Fonts\\YuGothR.ttc",
        "C:\\Windows\\Fonts\\msgothic.ttc",
        "C:\\Windows\\Fonts\\meiryo.ttc",
        nullptr
    };
#else
    const char* jp_fonts[] = {
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/LastResort.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        nullptr
    };
#endif

    for (const char** jp = jp_fonts; *jp; ++jp) {
        if (!FileExists(*jp)) continue;
        io.Fonts->AddFontFromFileTTF(*jp, font_size, &cfg, io.Fonts->GetGlyphRangesJapanese());
        break;
    }
}

}  // namespace

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
    ImGuiIO& io = ImGui::GetIO();
    constexpr float kFontSize = 15.0f;

    ImFontConfig base_cfg;
    base_cfg.PixelSnapH = true;

#ifdef _WIN32
    const char* regular_font = "C:\\Windows\\Fonts\\segoeui.ttf";
    const char* bold_font = "C:\\Windows\\Fonts\\segoeuib.ttf";

    if (FileExists(regular_font)) {
        io.Fonts->AddFontFromFileTTF(regular_font, kFontSize, &base_cfg);
    } else {
        io.Fonts->AddFontDefault(&base_cfg);
    }
#else
    io.Fonts->AddFontDefault(&base_cfg);
#endif

    ImFontConfig merge_cfg;
    merge_cfg.MergeMode = true;
    merge_cfg.PixelSnapH = true;
    MergeJapaneseGlyphs(io, kFontSize, merge_cfg);

#ifdef _WIN32
    ui::gsn::g_BoldFont = FileExists(bold_font)
                              ? io.Fonts->AddFontFromFileTTF(bold_font, kFontSize, &base_cfg)
                              : nullptr;
    if (ui::gsn::g_BoldFont) {
        MergeJapaneseGlyphs(io, kFontSize, merge_cfg);
    }
#endif

    if (!ui::gsn::g_BoldFont && !io.Fonts->Fonts.empty()) {
        ui::gsn::g_BoldFont = io.Fonts->Fonts[0];
    }
}

}  // namespace app