#include <gtest/gtest.h>

#include "ui/localization.h"

TEST(LocalizationTest, DefaultsToEnglishForUnknownLanguageCode) {
    EXPECT_EQ(ui::ParseLanguageCode(""), ui::Language::English);
    EXPECT_EQ(ui::ParseLanguageCode("fr"), ui::Language::English);
}

TEST(LocalizationTest, ParsesJapaneseLanguageCodes) {
    EXPECT_EQ(ui::ParseLanguageCode("ja"), ui::Language::Japanese);
    EXPECT_EQ(ui::ParseLanguageCode("ja-JP"), ui::Language::Japanese);
}

TEST(LocalizationTest, LooksUpEnglishAndJapaneseMessages) {
    ui::SetCurrentLanguage(ui::Language::English);
    EXPECT_STREQ(ui::Tr(ui::MessageId::FileMenu), "File");

    ui::SetCurrentLanguage(ui::Language::Japanese);
    EXPECT_STREQ(ui::Tr(ui::MessageId::FileMenu), "\xE3\x83\x95" "\xE3\x82\xA1" "\xE3\x82\xA4" "\xE3\x83\xAB");

    ui::SetCurrentLanguage(ui::Language::English);
}

TEST(LocalizationTest, FallsBackToEnglishForMissingJapaneseEntry) {
    ui::SetCurrentLanguage(ui::Language::Japanese);
    EXPECT_STREQ(
        ui::Tr(ui::MessageId::AiPrivacyNotice),
        "AI features may send selected safety case content and prompts to the configured AI provider. Assurance Forge will not send project data automatically; data is sent only when you explicitly use an AI action.");

    ui::SetCurrentLanguage(ui::Language::English);
}
