#include <gtest/gtest.h>

#include "ai/ai_settings.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path value) : path(std::move(value)) {}
    ~TempDir() { std::filesystem::remove_all(path); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::filesystem::path MakeTempDir() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("assurance_forge_ai_settings_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

}  // namespace

TEST(AiSettingsTest, MissingFileReturnsSafeDefaults) {
    TempDir temp(MakeTempDir());
    ai::AiSettingsStore store(temp.path / "settings.json");

    ai::AiProviderSettings settings = store.Load();

    EXPECT_FALSE(settings.enabled);
    EXPECT_EQ(settings.provider, ai::AiProviderId::OpenAI);
    EXPECT_EQ(settings.model, ai::kDefaultOpenAiModel);
    EXPECT_TRUE(settings.sendProjectDataOnlyOnExplicitUserAction);
}

TEST(AiSettingsTest, SavesAndLoadsNonSecretPreferences) {
    TempDir temp(MakeTempDir());
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
    TempDir temp(MakeTempDir());
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
    TempDir temp(MakeTempDir());
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