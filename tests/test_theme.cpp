#include "ui/theme.h"

#include <gtest/gtest.h>

#include <string_view>

namespace {

TEST(AppTheme, DisplayNamesAndSerializationAreStable) {
    EXPECT_EQ(std::string_view(ui::GetThemeDisplayName(ui::AppTheme::Dark)), "Dark");
    EXPECT_EQ(std::string_view(ui::GetThemeDisplayName(ui::AppTheme::Light)), "Light");
    EXPECT_EQ(ui::SerializeAppTheme(ui::AppTheme::Dark), "Dark");
    EXPECT_EQ(ui::SerializeAppTheme(ui::AppTheme::Light), "Light");
    EXPECT_EQ(ui::GetDefaultAppTheme(), ui::AppTheme::Dark);
}

TEST(AppTheme, ParsesSupportedAndLegacyLightNames) {
    EXPECT_EQ(ui::ParseAppTheme("Light"), ui::AppTheme::Light);
    EXPECT_EQ(ui::ParseAppTheme("light"), ui::AppTheme::Light);
    EXPECT_EQ(ui::ParseAppTheme("Forge Light"), ui::AppTheme::Light);
    EXPECT_EQ(ui::ParseAppTheme("Bright"), ui::AppTheme::Light);
    EXPECT_EQ(ui::ParseAppTheme("ImGuiColorsLight"), ui::AppTheme::Light);
    EXPECT_EQ(ui::ParseAppTheme("LightRounded"), ui::AppTheme::Light);
    EXPECT_EQ(ui::ParseAppTheme("WhiteIsWhite"), ui::AppTheme::Light);
}

TEST(AppTheme, FallsBackToDarkForUnknownAndLegacyDarkNames) {
    EXPECT_EQ(ui::ParseAppTheme("Dark"), ui::AppTheme::Dark);
    EXPECT_EQ(ui::ParseAppTheme("DarculaDarker"), ui::AppTheme::Dark);
    EXPECT_EQ(ui::ParseAppTheme("ImGuiColorsClassic"), ui::AppTheme::Dark);
    EXPECT_EQ(ui::ParseAppTheme(""), ui::AppTheme::Dark);
    EXPECT_EQ(ui::ParseAppTheme("not-a-theme"), ui::AppTheme::Dark);
}

TEST(AppTheme, SemanticPaletteSwitchesWithAppTheme) {
    ui::ApplyAppTheme(ui::AppTheme::Dark);
    const ui::Theme dark = ui::GetTheme();
    EXPECT_EQ(ui::GetCurrentAppTheme(), ui::AppTheme::Dark);

    ui::ApplyAppTheme(ui::AppTheme::Light);
    const ui::Theme light = ui::GetTheme();
    EXPECT_EQ(ui::GetCurrentAppTheme(), ui::AppTheme::Light);

    EXPECT_NE(dark.bg_app, light.bg_app);
    EXPECT_NE(dark.surface_1, light.surface_1);
    EXPECT_NE(dark.text_primary, light.text_primary);
    EXPECT_NE(dark.canvas_bg, light.canvas_bg);
}

TEST(AppTheme, AppliesReadableFloatingWindowAndTreeStyle) {
    ImGui::CreateContext();

    ui::ApplyAppTheme(ui::AppTheme::Dark);
    const ImGuiStyle& style = ImGui::GetStyle();

    EXPECT_FLOAT_EQ(style.WindowBorderSize, 1.0f);
    EXPECT_FLOAT_EQ(style.PopupBorderSize, 1.0f);
    EXPECT_FLOAT_EQ(style.ChildBorderSize, 0.0f);
    EXPECT_FLOAT_EQ(style.FrameBorderSize, 0.0f);
    EXPECT_FLOAT_EQ(style.IndentSpacing, 14.0f);

    ImGui::DestroyContext();
}

TEST(AppTheme, LightThemeSelectedTabIsVisiblyDistinct) {
    ImGui::CreateContext();

    ui::ApplyAppTheme(ui::AppTheme::Light);
    const ImVec4 inactive_tab = ImGui::GetStyleColorVec4(ImGuiCol_Tab);
    const ImVec4 selected_tab = ImGui::GetStyleColorVec4(ImGuiCol_TabSelected);
    const ImVec4 selected_overline = ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline);

    EXPECT_LT(selected_tab.x, inactive_tab.x - 0.05f);
    EXPECT_LT(selected_tab.y, inactive_tab.y - 0.02f);
    EXPECT_GT(selected_tab.z - selected_tab.x, (inactive_tab.z - inactive_tab.x) + 0.08f);
    EXPECT_GT(selected_overline.w, 0.5f);

    ImGui::DestroyContext();
}

TEST(AppTheme, LightThemeRoleTextColorsAreDarkerThanNodeFills) {
    ui::ApplyAppTheme(ui::AppTheme::Light);
    const ui::Theme& theme = ui::GetTheme();

    EXPECT_NE(theme.node_claim, theme.node_claim_text);
    EXPECT_NE(theme.node_strategy, theme.node_strategy_text);
    EXPECT_NE(theme.node_solution, theme.node_solution_text);

    const ImVec4 claim_fill = ImGui::ColorConvertU32ToFloat4(theme.node_claim);
    const ImVec4 claim_text = ImGui::ColorConvertU32ToFloat4(theme.node_claim_text);
    const ImVec4 strategy_fill = ImGui::ColorConvertU32ToFloat4(theme.node_strategy);
    const ImVec4 strategy_text = ImGui::ColorConvertU32ToFloat4(theme.node_strategy_text);
    const ImVec4 solution_fill = ImGui::ColorConvertU32ToFloat4(theme.node_solution);
    const ImVec4 solution_text = ImGui::ColorConvertU32ToFloat4(theme.node_solution_text);

    EXPECT_LT(claim_text.x + claim_text.y + claim_text.z, claim_fill.x + claim_fill.y + claim_fill.z);
    EXPECT_LT(strategy_text.x + strategy_text.y + strategy_text.z, strategy_fill.x + strategy_fill.y + strategy_fill.z);
    EXPECT_LT(solution_text.x + solution_text.y + solution_text.z, solution_fill.x + solution_fill.y + solution_fill.z);
}

} // namespace
