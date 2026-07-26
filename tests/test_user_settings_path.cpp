#include "core/user_settings.h"

#include "ai/ai_settings.h"

#include <gtest/gtest.h>

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
