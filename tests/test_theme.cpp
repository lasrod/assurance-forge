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

} // namespace
