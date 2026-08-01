#include "core/user_settings.h"

#include "ai/ai_settings.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

// The MCP consent gate reads the `mcp` section of the same settings file the AI
// settings live in, but the layer gate forbids `mcp/` from including `ai/`, so
// the path rules exist in two places. If they ever diverge, the consent gate
// reads a file nobody writes and silently reports "not enabled" forever -- or,
// worse, a future writer targets the other path and the gate reads stale
// permission. Pin them together here; this test is the reason duplicating the
// path is acceptable.
TEST(UserSettingsPath, MatchesTheAiSettingsStorePath) {
    EXPECT_EQ(core::UserSettingsFilePath(), ai::AiSettingsStore::DefaultSettingsPath());
}

TEST(UserSettingsPath, IsAbsoluteAndNamesTheSettingsFile) {
    const std::filesystem::path path = core::UserSettingsFilePath();
    EXPECT_TRUE(path.is_absolute());
    EXPECT_EQ(path.filename(), "settings.json");
}

} // namespace

// ---------------------------------------------------------------------------
// Sibling-section preservation
//
// One settings file holds several independent sections written by components
// that cannot see each other. `AiSettingsStore::Save` used to build a fresh
// document containing only "ai" and truncate the file, so saving any AI
// preference silently deleted the "mcp" section and turned the MCP server off --
// with no diagnostic beyond the user's AI client suddenly refusing. These are the
// regression that would have caught it.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path SettingsScratchFile(const std::string& stem) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "af_user_settings_tests";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / (stem + ".json");
    std::filesystem::remove(path);
    return path;
}

} // namespace

TEST(UserSettings, UpdateSectionPreservesSiblingSections) {
    const std::filesystem::path path = SettingsScratchFile("siblings");
    std::string error;

    ASSERT_TRUE(core::UpdateUserSettingsSection(path, "mcp", {{"enabled", true}}, error)) << error;
    ASSERT_TRUE(core::UpdateUserSettingsSection(path, "ai", {{"model", "gpt-5.5"}}, error)) << error;

    EXPECT_EQ(core::ReadUserSettingsSection(path, "mcp")["enabled"], true);
    EXPECT_EQ(core::ReadUserSettingsSection(path, "ai")["model"], "gpt-5.5");
}

TEST(UserSettings, ReadSectionFailsSoftlyForMissingFileSectionAndMalformedDocument) {
    EXPECT_TRUE(core::ReadUserSettingsSection(SettingsScratchFile("absent"), "mcp").is_null());

    const std::filesystem::path no_section = SettingsScratchFile("no_section");
    std::string error;
    ASSERT_TRUE(core::UpdateUserSettingsSection(no_section, "ai", {{"model", "x"}}, error)) << error;
    EXPECT_TRUE(core::ReadUserSettingsSection(no_section, "mcp").is_null());

    const std::filesystem::path malformed = SettingsScratchFile("malformed");
    std::ofstream(malformed, std::ios::trunc) << "{ not json";
    EXPECT_TRUE(core::ReadUserSettingsSection(malformed, "mcp").is_null());
}

// The exact regression: enable MCP by hand, then save AI settings the way the
// preferences panel does, and MCP must still be enabled.
TEST(UserSettings, SavingAiSettingsDoesNotDisableMcp) {
    const std::filesystem::path path = SettingsScratchFile("ai_save_keeps_mcp");
    std::string error;
    ASSERT_TRUE(core::UpdateUserSettingsSection(path, "mcp", {{"enabled", true}}, error)) << error;

    ai::AiSettingsStore store(path);
    ai::AiProviderSettings settings;
    settings.provider = ai::AiProviderId::OpenAI;
    settings.model = "gpt-5.5";
    settings.enabled = true;
    ASSERT_TRUE(store.Save(settings, error)) << error;

    EXPECT_EQ(core::ReadUserSettingsSection(path, "mcp")["enabled"], true)
        << "saving AI settings deleted the MCP consent flag";
    EXPECT_EQ(store.Load(nullptr).model, "gpt-5.5") << "the AI section did not survive its own save";
}
