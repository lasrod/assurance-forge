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
    "Export",
    "GSN SVG",
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
    "Language",
    "Show FPS",
    "High culling",
    "Medium culling",
    "Low culling",
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
    "AI features may send selected safety case content and prompts to the configured AI provider. Assurance Forge will "
    "not send project data automatically; data is sent only when you explicitly use an AI action.",
    "OK",
    "Walkthroughs",
    "Walkthroughs are not yet implemented.",
    "Import SACM",
    "Import SACM is not yet implemented.",
    "Forge Confidence in Safety",
    "Start",
    "Open Recent Projects",
    "No recent projects.",
    "Create Empty Assurance Project",
    "Start with a blank assurance project workspace",
    "Create Assurance Project from Template",
    "Create a project from a predefined assurance case template",
    "Create Assurance Project from Template is not yet implemented.",
    "Open Project",
    "Open an existing Assurance Forge project",
    "Import SACM",
    "Import a SACM XML assurance case",
    "Get started with Assurance Forge",
    "Create, inspect, and navigate a safety case",
    "Learn the Fundamentals",
    "GSN structure, SACM imports, evidence, and registers",
    "Prepare a Conformance Review",
    "Trace claims, evidence, and review outputs",
};

const Catalog kJapaneseCatalog = {
    u8"ファイル",
    u8"空の保証プロジェクトを作成",
    u8"プロジェクトを開く",
    u8"プロジェクトを保存",
    u8"エクスポート",
    u8"GSN SVG",
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
    u8"言語",
    u8"FPS を表示",
    u8"高カリング",
    u8"中カリング",
    u8"低カリング",
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
    u8"OK",
    u8"ウォークスルー",
    u8"ウォークスルーは未実装です。",
    u8"SACM をインポート",
    u8"SACM のインポートは未実装です。",
    u8"安全への確信を鍛える",
    u8"開始",
    u8"最近のプロジェクトを開く",
    u8"最近のプロジェクトはありません。",
    u8"空の保証プロジェクトを作成",
    u8"空の保証プロジェクトワークスペースから開始します",
    u8"テンプレートから保証プロジェクトを作成",
    u8"定義済みの保証ケーステンプレートからプロジェクトを作成します",
    u8"テンプレートから保証プロジェクトを作成は未実装です。",
    u8"プロジェクトを開く",
    u8"既存の Assurance Forge プロジェクトを開きます",
    u8"SACM をインポート",
    u8"SACM XML 保証ケースをインポートします",
    u8"Assurance Forge の開始ガイド",
    u8"安全ケースの作成・確認・操作を行います",
    u8"基本を学ぶ",
    u8"GSN 構造、SACM インポート、エビデンス、レジスターを学びます",
    u8"適合性レビューを準備する",
    u8"主張、エビデンス、レビュー出力を追跡します",
};

Language g_currentLanguage = Language::English;

const Catalog& CatalogFor(Language language) {
    return language == Language::Japanese ? kJapaneseCatalog : kEnglishCatalog;
}

} // namespace

Language CurrentLanguage() {
    return g_currentLanguage;
}

void SetCurrentLanguage(Language language) {
    g_currentLanguage = language;
}

Language ParseLanguageCode(const std::string& code) {
    if (code == "ja" || code == "ja-JP" || code == "jp")
        return Language::Japanese;
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
    if (index >= kMessageCount)
        return "";
    const char* localized = CatalogFor(g_currentLanguage)[index];
    if (localized && localized[0] != '\0')
        return localized;
    const char* english = kEnglishCatalog[index];
    return english ? english : "";
}

} // namespace ui
