#include "app/ai_error_text.h"

#include "app/app_runtime_state.h"
#include "ui/i18n/localization.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// AI provider errors reach users as text the `ai` layer writes in English
// (issue #451). These hold the app-side translation to that English, and check
// that what a Japanese user sees is actually Japanese.

namespace {

namespace i18n = ui::i18n;

const std::vector<ai::AiErrorCode>& EveryCode() {
    static const std::vector<ai::AiErrorCode> codes = {
        ai::AiErrorCode::None,
        ai::AiErrorCode::Disabled,
        ai::AiErrorCode::MissingApiKey,
        ai::AiErrorCode::SecureStoreUnavailable,
        ai::AiErrorCode::AuthenticationFailed,
        ai::AiErrorCode::NetworkError,
        ai::AiErrorCode::Timeout,
        ai::AiErrorCode::RateLimited,
        ai::AiErrorCode::QuotaExhausted,
        ai::AiErrorCode::InvalidModel,
        ai::AiErrorCode::MalformedResponse,
        ai::AiErrorCode::ProviderError,
        ai::AiErrorCode::SettingsError,
        ai::AiErrorCode::Unknown,
    };
    return codes;
}

// Loads the committed catalogue for `language`, and restores English after the
// test so no other suite inherits Japanese.
class AiErrorTextTest : public ::testing::Test {
protected:
    static void UseLanguage(i18n::Language language) {
        i18n::LocalizationConfig config;
        config.localeDirectory = "assets/locale"; // tests run from CMAKE_SOURCE_DIR
        config.language = language;
        i18n::Initialize(config);
    }
    void TearDown() override {
        UseLanguage(i18n::Language::English);
    }
};

} // namespace

// The app-side literals are the catalogue's msgids. If one drifts from the ai
// layer's text, the English UI changes wording and the Japanese entry for the
// old wording is never looked up again.
TEST_F(AiErrorTextTest, EnglishIsTheAiLayersTextForEveryCode) {
    UseLanguage(i18n::Language::English);
    for (const ai::AiErrorCode code : EveryCode())
        EXPECT_EQ(app::LocalizedAiErrorCode(code), ai::ToString(code)) << ai::ToString(code);
}

TEST_F(AiErrorTextTest, EveryErrorCodeIsTranslatedIntoJapanese) {
    UseLanguage(i18n::Language::Japanese);
    for (const ai::AiErrorCode code : EveryCode())
        EXPECT_NE(app::LocalizedAiErrorCode(code), ai::ToString(code)) << ai::ToString(code);
    EXPECT_EQ(app::LocalizedAiErrorCode(ai::AiErrorCode::RateLimited), "レート制限に達しました");
}

// Authentication, rate-limit and quota failures arrive with the code's own text
// as the message, so the message is the thing to translate.
TEST_F(AiErrorTextTest, AMessageThatRepeatsTheCodeIsReplacedByItsTranslation) {
    UseLanguage(i18n::Language::Japanese);
    EXPECT_EQ(app::LocalizedAiErrorMessage(ai::AiErrorCode::RateLimited, "Rate limit reached"),
              "レート制限に達しました");
    // The provider's own copy carries a full stop that ai::ToString does not.
    EXPECT_EQ(app::LocalizedAiErrorMessage(ai::AiErrorCode::MissingApiKey, "Missing API key."), "API キーがありません");
    EXPECT_EQ(app::LocalizedAiErrorMessage(ai::AiErrorCode::QuotaExhausted, ""),
              "AI プロバイダーのアカウントにクレジットが残っていません");
}

// A network failure's message is the network library's reason. Dropping it
// would leave the user a translated "Network error" and no way to act on it.
TEST_F(AiErrorTextTest, ASpecificMessageIsKeptAfterTheTranslatedCode) {
    UseLanguage(i18n::Language::Japanese);
    EXPECT_EQ(app::LocalizedAiErrorMessage(ai::AiErrorCode::NetworkError, "Could not resolve host: api.openai.com"),
              "ネットワークエラー: Could not resolve host: api.openai.com");
}

TEST_F(AiErrorTextTest, TheConnectionTestsOwnProgressAndSuccessTextIsTranslated) {
    UseLanguage(i18n::Language::Japanese);
    EXPECT_EQ(
        app::LocalizedAiStatus(ai::MakeStatus(ai::AiTaskState::Running, ai::AiErrorCode::None, "Testing connection..."))
            .message,
        "接続をテスト中…");
    EXPECT_EQ(app::LocalizedAiStatus(ai::SuccessStatus("Connection successful.")).message, "接続に成功しました。");

    const ai::AiConnectionStatus failed =
        app::LocalizedAiStatus(ai::ErrorStatus(ai::AiErrorCode::AuthenticationFailed, "Authentication failed"));
    EXPECT_EQ(failed.message, "認証に失敗しました");
    EXPECT_EQ(failed.errorCode, ai::AiErrorCode::AuthenticationFailed) << "only the text changes";
}

// A success message `app` built is already translated, and must come through
// untouched rather than be looked up a second time.
TEST_F(AiErrorTextTest, AnUnrecognisedSuccessMessageIsLeftAlone) {
    UseLanguage(i18n::Language::Japanese);
    EXPECT_EQ(app::LocalizedAiStatus(ai::SuccessStatus("AI 設定を保存しました。")).message, "AI 設定を保存しました。");
}

// Startup loads the AI settings before the user's language, so a settings
// warning is stored while English is still in force. It must be stored as the
// ai layer wrote it and translated when shown, or it stays English for good.
TEST_F(AiErrorTextTest, AnAiLayerStatusStoredBeforeTheLanguageLoadsIsShownInIt) {
    UseLanguage(i18n::Language::English);
    app::AiUiState state;
    state.SetAiLayerStatus(ai::ErrorStatus(ai::AiErrorCode::RateLimited, "Rate limit reached"));
    EXPECT_EQ(state.connection_status.message, "Rate limit reached") << "stored untranslated";

    UseLanguage(i18n::Language::Japanese);
    ASSERT_TRUE(state.connection_status_from_ai);
    EXPECT_EQ(app::LocalizedAiStatus(state.connection_status).message, "レート制限に達しました");

    state.SetTranslatedStatus(ai::SuccessStatus("AI 設定を保存しました。"));
    EXPECT_FALSE(state.connection_status_from_ai) << "an app-built status is shown as stored";
}
