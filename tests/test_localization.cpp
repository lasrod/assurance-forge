#include "ui/localization.h"

#include <gtest/gtest.h>

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
    EXPECT_STREQ(ui::Tr(ui::MessageId::FileMenu),
                 "\xE3\x83\x95"
                 "\xE3\x82\xA1"
                 "\xE3\x82\xA4"
                 "\xE3\x83\xAB");

    ui::SetCurrentLanguage(ui::Language::English);
}

TEST(LocalizationTest, FallsBackToEnglishForMissingJapaneseEntry) {
    ui::SetCurrentLanguage(ui::Language::Japanese);
    EXPECT_STREQ(
        ui::Tr(ui::MessageId::AiPrivacyNotice),
        "AI features may send selected safety case content and prompts to the configured AI provider. Assurance Forge "
        "will not send project data automatically; data is sent only when you explicitly use an AI action.");

    ui::SetCurrentLanguage(ui::Language::English);
}

TEST(LocalizationTest, LooksUpNewWelcomeStringsInEnglish) {
    ui::SetCurrentLanguage(ui::Language::English);

    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeTagline), "Forge Confidence in Safety");
    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeNoRecentProjects), "No recent projects.");
    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeActionCreateEmptyTitle), "Create Empty Assurance Project");
    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeWalkthroughGetStartedTitle), "Get started with Assurance Forge");
    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeWalkthroughConformanceSubtitle),
                 "Trace claims, evidence, and review outputs");

    ui::SetCurrentLanguage(ui::Language::English);
}

TEST(LocalizationTest, LooksUpNewWelcomeStringsInJapanese) {
    ui::SetCurrentLanguage(ui::Language::Japanese);

    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeTagline), u8"安全への確信を鍛える");
    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeNoRecentProjects), u8"最近のプロジェクトはありません。");
    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeActionCreateEmptyTitle), u8"空の保証プロジェクトを作成");
    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeWalkthroughGetStartedTitle), u8"Assurance Forge の開始ガイド");
    EXPECT_STREQ(ui::Tr(ui::MessageId::WelcomeWalkthroughConformanceSubtitle),
                 u8"主張、エビデンス、レビュー出力を追跡します");

    ui::SetCurrentLanguage(ui::Language::English);
}
