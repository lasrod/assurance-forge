#include "ui/localization.h"

#include <array>
#include <cstddef>

namespace ui {
namespace {

constexpr size_t kMessageCount = static_cast<size_t>(MessageId::Count);

using Catalog = std::array<const char*, kMessageCount>;

constexpr size_t Index(MessageId id) {
    return static_cast<size_t>(id);
}

const Catalog kEnglishCatalog = {
    "File",
    "Create Empty Assurance Project",
    "Open Project",
    "Save Project",
    "Exit",
    "Add",
    "New GSN / SACM File",
    "New Evidence Register",
    "New J3377 CAE Register",
    "Edit",
    "Preferences...",
    "Preferences",
    "View",
    "GSN Canvas",
    "CSE Register",
    "Evidence Register",
    "Welcome Screen",
    "Appearance",
    "Theme",
    "Theme Tweaks",
    "Language",
    "English",
    "Japanese",
    "AI",
    "AI settings are unavailable.",
    "Enable AI support",
    "Provider",
    "Model",
    "Save AI Settings",
    "API Key",
    "Secure storage is unavailable on this platform.",
    "Key stored: ********",
    "No API key stored.",
    "Update Key",
    "Save Key",
    "Remove Key",
    "Test Connection",
    "AI features may send selected safety case content and prompts to the configured AI provider. Assurance Forge will not send project data automatically; data is sent only when you explicitly use an AI action.",
};

const Catalog kJapaneseCatalog = {
    u8"ファイル",
    u8"空の保証プロジェクトを作成",
    u8"プロジェクトを開く",
    u8"プロジェクトを保存",
    u8"終了",
    u8"追加",
    u8"新規 GSN / SACM ファイル",
    u8"新規エビデンス登録簿",
    u8"新規 J3377 CAE 登録簿",
    u8"編集",
    u8"設定...",
    u8"設定",
    u8"表示",
    u8"GSN キャンバス",
    u8"CSE 登録簿",
    u8"エビデンス登録簿",
    u8"ウェルカム画面",
    u8"外観",
    u8"テーマ",
    u8"テーマ調整",
    u8"言語",
    u8"英語",
    u8"日本語",
    u8"AI",
    u8"AI 設定を使用できません。",
    u8"AI サポートを有効にする",
    u8"プロバイダー",
    u8"モデル",
    u8"AI 設定を保存",
    u8"API キー",
    u8"このプラットフォームでは安全な保存を使用できません。",
    u8"キー保存済み: ********",
    u8"保存済み API キーはありません。",
    u8"キーを更新",
    u8"キーを保存",
    u8"キーを削除",
    u8"接続をテスト",
    "",
};

Language g_currentLanguage = Language::English;

const Catalog& CatalogFor(Language language) {
    return language == Language::Japanese ? kJapaneseCatalog : kEnglishCatalog;
}

}  // namespace

Language CurrentLanguage() {
    return g_currentLanguage;
}

void SetCurrentLanguage(Language language) {
    g_currentLanguage = language;
}

Language ParseLanguageCode(const std::string& code) {
    if (code == "ja" || code == "ja-JP" || code == "jp") return Language::Japanese;
    return Language::English;
}

const char* LanguageCode(Language language) {
    return language == Language::Japanese ? "ja" : "en";
}

const char* LanguageDisplayName(Language language) {
    return Tr(language == Language::Japanese ? MessageId::Japanese : MessageId::English);
}

const char* Tr(MessageId id) {
    size_t index = Index(id);
    if (index >= kMessageCount) return "";
    const char* localized = CatalogFor(g_currentLanguage)[index];
    if (localized && localized[0] != '\0') return localized;
    const char* english = kEnglishCatalog[index];
    return english ? english : "";
}

}  // namespace ui
