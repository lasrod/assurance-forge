#include "ai/ai_settings.h"
#include "support/temp_files.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <utility>

namespace {

using test_support::TempDir;

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

TEST(AiSettingsTest, MissingFileReturnsSafeDefaults) {
    TempDir temp(test_support::UniqueTempDirectory("ai_settings"));
    ai::AiSettingsStore store(temp.path / "settings.json");

    ai::AiProviderSettings settings = store.Load();

    EXPECT_FALSE(settings.enabled);
    EXPECT_EQ(settings.provider, ai::AiProviderId::OpenAI);
    EXPECT_EQ(settings.model, ai::kDefaultOpenAiModel);
    EXPECT_TRUE(settings.sendProjectDataOnlyOnExplicitUserAction);
}

TEST(AiSettingsTest, SavesAndLoadsNonSecretPreferences) {
    TempDir temp(test_support::UniqueTempDirectory("ai_settings"));
    auto path = temp.path / "settings.json";
    ai::AiSettingsStore store(path);
    ai::AiProviderSettings settings;
    settings.enabled = true;
    settings.model = "gpt-test";

    std::string error;
    ASSERT_TRUE(store.Save(settings, error)) << error;

    ai::AiProviderSettings loaded = store.Load();
    EXPECT_TRUE(loaded.enabled);
    EXPECT_EQ(loaded.provider, ai::AiProviderId::OpenAI);
    EXPECT_EQ(loaded.model, "gpt-test");
    EXPECT_TRUE(loaded.sendProjectDataOnlyOnExplicitUserAction);
}

TEST(AiSettingsTest, DoesNotPersistApiKeyLikeFields) {
    TempDir temp(test_support::UniqueTempDirectory("ai_settings"));
    auto path = temp.path / "settings.json";
    ai::AiSettingsStore store(path);
    ai::AiProviderSettings settings;
    settings.enabled = true;
    settings.model = "gpt-test";

    std::string error;
    ASSERT_TRUE(store.Save(settings, error)) << error;

    std::string text = ReadFile(path);
    EXPECT_EQ(text.find("apiKey"), std::string::npos);
    EXPECT_EQ(text.find("Authorization"), std::string::npos);
    EXPECT_EQ(text.find("sk-"), std::string::npos);
}

TEST(AiSettingsTest, MalformedFileFallsBackToDefaults) {
    TempDir temp(test_support::UniqueTempDirectory("ai_settings"));
    auto path = temp.path / "settings.json";
    {
        std::ofstream file(path, std::ios::binary);
        file << "{ invalid json";
    }

    ai::AiSettingsStore store(path);
    std::string warning;
    ai::AiProviderSettings settings = store.Load(&warning);

    EXPECT_FALSE(settings.enabled);
    EXPECT_EQ(settings.model, ai::kDefaultOpenAiModel);
    EXPECT_FALSE(warning.empty());
}