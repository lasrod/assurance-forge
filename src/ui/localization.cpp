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
    "Undo",
    "Reached snapshot or baseline \u2014 restore from history to go further back.",
};

const Catalog kJapaneseCatalog = {
    "ファイル",
    "空の保証プロジェクトを作成",
    "プロジェクトを開く",
    "プロジェクトを保存",
    "エクスポート",
    "GSN SVG",
    "終了",
    "追加",
    "新規 GSN / SACM ファイル",
    "新規エビデンス登録簿",
    "新規 J3377 CAE 登録簿",
    "編集",
    "設定...",
    "設定",
    "表示",
    "GSN キャンバス",
    "CSE 登録簿",
    "エビデンス登録簿",
    "ウェルカム画面",
    "外観",
    "テーマ",
    "言語",
    "FPS を表示",
    "高カリング",
    "中カリング",
    "低カリング",
    "英語",
    "日本語",
    "AI",
    "AI 設定を使用できません。",
    "AI サポートを有効にする",
    "プロバイダー",
    "モデル",
    "AI 設定を保存",
    "API キー",
    "このプラットフォームでは安全な保存を使用できません。",
    "キー保存済み: ********",
    "保存済み API キーはありません。",
    "キーを更新",
    "キーを保存",
    "キーを削除",
    "接続をテスト",
    "",
    "OK",
    "ウォークスルー",
    "ウォークスルーは未実装です。",
    "SACM をインポート",
    "SACM のインポートは未実装です。",
    "安全への確信を鍛える",
    "開始",
    "最近のプロジェクトを開く",
    "最近のプロジェクトはありません。",
    "空の保証プロジェクトを作成",
    "空の保証プロジェクトワークスペースから開始します",
    "テンプレートから保証プロジェクトを作成",
    "定義済みの保証ケーステンプレートからプロジェクトを作成します",
    "テンプレートから保証プロジェクトを作成は未実装です。",
    "プロジェクトを開く",
    "既存の Assurance Forge プロジェクトを開きます",
    "SACM をインポート",
    "SACM XML 保証ケースをインポートします",
    "Assurance Forge の開始ガイド",
    "安全ケースの作成・確認・操作を行います",
    "基本を学ぶ",
    "GSN 構造、SACM インポート、エビデンス、レジスターを学びます",
    "適合性レビューを準備する",
    "主張、エビデンス、レビュー出力を追跡します",
    "元に戻す",
    "スナップショットまたはベースラインに到達しました \u2014 さらに戻すには履歴から復元してください。",
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
