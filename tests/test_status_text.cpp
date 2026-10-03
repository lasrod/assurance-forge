// Status messages reported as a msgid plus arguments (#252).
//
// `core` cannot call `ui::i18n`, so `core::AppState::set_status` records what it
// said and `ui::LocalizedStatusMessage` translates it where it is shown.

#include "core/app_state.h"
#include "core/status_text.h"
#include "support/temp_files.h"
#include "ui/i18n/localization.h"
#include "ui/localized_status.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

void UseEnglish() {
    ui::i18n::LocalizationConfig config;
    config.localeDirectory = "assets/locale"; // tests run from CMAKE_SOURCE_DIR
    config.language = ui::i18n::Language::English;
    ui::i18n::Initialize(config);
}

// The committed Japanese catalogue, so these tests fail if a `core` msgid loses
// its translation.
class LocalizedStatusTest : public ::testing::Test {
protected:
    void SetUp() override {
        ui::i18n::LocalizationConfig config;
        config.localeDirectory = "assets/locale";
        config.language = ui::i18n::Language::Japanese;
        ASSERT_TRUE(ui::i18n::Initialize(config));
    }
    void TearDown() override {
        UseEnglish();
    }
};

} // namespace

TEST(StatusTextTest, FillsPositionalPlaceholders) {
    EXPECT_EQ(core::FormatStatusText("Saved to: {0}", {"a.sacm"}), "Saved to: a.sacm");
    EXPECT_EQ(core::FormatStatusText("{1} from {0}", {"first", "second"}), "second from first");
    EXPECT_EQ(core::FormatStatusText("No placeholders.", {}), "No placeholders.");
}

TEST(StatusTextTest, TreatsDoubledBracesAsLiteral) {
    EXPECT_EQ(core::FormatStatusText("{{0}} is {0}", {"x"}), "{0} is x");
}

TEST(StatusTextTest, LeavesUnusablePlaceholdersAsWritten) {
    EXPECT_EQ(core::FormatStatusText("Missing {1}", {"only"}), "Missing {1}");
    EXPECT_EQ(core::FormatStatusText("Named {name}", {"x"}), "Named {name}");
    EXPECT_EQ(core::FormatStatusText("Empty {} and open {0", {"x"}), "Empty {} and open {0");
}

TEST(StatusTextTest, DoesNotRescanArgumentText) {
    // A file path may contain braces; it must not be read as a placeholder.
    EXPECT_EQ(core::FormatStatusText("Saved to: {0} ({1})", {"C:/{1}/a.sacm", "ok"}), "Saved to: C:/{1}/a.sacm (ok)");
}

TEST(AppStateStatusTest, SetStatusKeepsTheEnglishTextAndRecordsItsSource) {
    core::AppState state;
    EXPECT_FALSE(state.status_source_is_current());

    state.set_status(AF_TR_NOOP("Project saved: {0}"), {"Blender"});

    EXPECT_EQ(state.status_message, "Project saved: Blender");
    EXPECT_EQ(state.status_source.msgid, "Project saved: {0}");
    EXPECT_EQ(state.status_source.arguments, std::vector<std::string>{"Blender"});
    EXPECT_TRUE(state.status_source_is_current());
}

TEST(AppStateStatusTest, OverwritingTheMessageRetiresTheSource) {
    core::AppState state;
    state.set_status(AF_TR_NOOP("Project saved: {0}"), {"Blender"});

    state.status_message = "Undid: add goal";
    EXPECT_FALSE(state.status_source_is_current());

    state.status_message.clear();
    EXPECT_FALSE(state.status_source_is_current());
}

TEST(AppStateStatusTest, CoreOperationsReportThroughSetStatus) {
    core::AppState state;

    EXPECT_FALSE(state.save_project());
    EXPECT_EQ(state.status_message, "Create or open a project first.");
    EXPECT_TRUE(state.status_source_is_current());

    // A child of a directory this test owns, so nothing else can have put a
    // project there.
    const test_support::TempDir temp(test_support::UniqueTempDirectory("status_text"));
    const std::filesystem::path missing = temp.path / "no_such_project";
    EXPECT_FALSE(state.open_project(missing.string()));
    EXPECT_EQ(state.status_source.msgid, "Project open failed: {0}");
    EXPECT_TRUE(state.status_message.starts_with("Project open failed: ")) << state.status_message;
    EXPECT_TRUE(state.status_source_is_current());
}

TEST_F(LocalizedStatusTest, TranslatesACoreMessageAndFillsItsArguments) {
    core::AppState state;
    state.set_status(AF_TR_NOOP("Project saved: {0}"), {"Blender"});

    EXPECT_EQ(state.status_message, "Project saved: Blender") << "the stored text stays English for MCP and logs";
    EXPECT_EQ(ui::LocalizedStatusMessage(state), "プロジェクトを保存しました: Blender");
}

TEST_F(LocalizedStatusTest, ReordersArgumentsAsTheTranslationAsks) {
    core::AppState state;
    state.set_status(AF_TR_NOOP("Created project: {0} from {1}"), {"Blender", "source.sacm"});

    EXPECT_EQ(ui::LocalizedStatusMessage(state), "source.sacm からプロジェクトを作成しました: Blender");
}

TEST_F(LocalizedStatusTest, FollowsALanguageSwitch) {
    core::AppState state;
    state.set_status(AF_TR_NOOP("Opened: {0}"), {"arguments/main.sacm"});
    EXPECT_EQ(ui::LocalizedStatusMessage(state), "開きました: arguments/main.sacm");

    ASSERT_TRUE(ui::i18n::SetLanguage(ui::i18n::Language::English));
    EXPECT_EQ(ui::LocalizedStatusMessage(state), "Opened: arguments/main.sacm");
}

TEST_F(LocalizedStatusTest, ReturnsDirectlyWrittenTextUnchanged) {
    core::AppState state;
    state.set_status(AF_TR_NOOP("Project saved: {0}"), {"Blender"});

    // `app` writes text it has already translated; the stale source must not
    // replace it.
    state.status_message = "Undid: add goal";
    EXPECT_EQ(ui::LocalizedStatusMessage(state), "Undid: add goal");
}

TEST_F(LocalizedStatusTest, TranslatesAMessageARealOperationReported) {
    core::AppState state;
    EXPECT_FALSE(state.save_project());

    EXPECT_EQ(ui::LocalizedStatusMessage(state), "まずプロジェクトを作成するか開いてください。");
}
