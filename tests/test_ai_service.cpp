#include <gtest/gtest.h>

#include "ai/ai_service.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <utility>

namespace {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::filesystem::path value) : path(std::move(value)) {}
    ~TempDir() { std::filesystem::remove_all(path); }
};

std::filesystem::path MakeTempDir() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("assurance_forge_ai_service_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

class FakeSecretStore final : public ai::ISecretStore {
public:
    bool available = true;
    std::map<std::string, std::string> secrets;

    bool IsAvailable() const override { return available; }

    ai::SecretStoreResult SaveSecret(const std::string& service,
                                     const std::string& account,
                                     const std::string& secret) override {
        if (!available) return ai::SecretStoreFailure(ai::AiErrorCode::SecureStoreUnavailable, "No secure storage.");
        secrets[service + ":" + account] = secret;
        return ai::SecretStoreSuccess();
    }

    ai::SecretLoadResult LoadSecret(const std::string& service,
                                    const std::string& account) override {
        if (!available) return ai::SecretLoadFailure(ai::AiErrorCode::SecureStoreUnavailable, "No secure storage.");
        auto found = secrets.find(service + ":" + account);
        if (found == secrets.end()) return ai::SecretLoadSuccess(std::nullopt);
        return ai::SecretLoadSuccess(found->second);
    }

    ai::SecretStoreResult DeleteSecret(const std::string& service,
                                       const std::string& account) override {
        if (!available) return ai::SecretStoreFailure(ai::AiErrorCode::SecureStoreUnavailable, "No secure storage.");
        secrets.erase(service + ":" + account);
        return ai::SecretStoreSuccess();
    }
};

class FakeProvider final : public ai::IAiProvider {
public:
    ai::AiProviderId ProviderId() const override { return ai::AiProviderId::OpenAI; }

    ai::AiConnectionStatus TestConnection(const ai::AiProviderSettings&, const std::string& api_key) override {
        if (api_key.empty()) return ai::ErrorStatus(ai::AiErrorCode::MissingApiKey, "Missing API key.");
        return ai::SuccessStatus("Connection successful.");
    }

    ai::AiResponse Generate(const ai::AiProviderSettings&, const ai::AiRequest&, const std::string& api_key) override {
        ai::AiResponse response;
        response.success = !api_key.empty();
        response.text = response.success ? "ok" : "";
        response.errorCode = response.success ? ai::AiErrorCode::None : ai::AiErrorCode::MissingApiKey;
        response.errorMessage = response.success ? "" : "Missing API key.";
        return response;
    }
};

struct TestAiEnvironment {
    std::shared_ptr<FakeSecretStore> secret_store;
    std::shared_ptr<FakeProvider> provider;
    ai::AiService service;

    explicit TestAiEnvironment(const std::filesystem::path& settings_path)
        : secret_store(std::make_shared<FakeSecretStore>()),
          provider(std::make_shared<FakeProvider>()),
          service(std::make_shared<ai::AiSettingsStore>(settings_path), secret_store, provider) {}
};

}  // namespace

TEST(AiServiceTest, DisabledSettingsBlockConnectionTest) {
    TempDir temp(MakeTempDir());
    TestAiEnvironment env(temp.path / "settings.json");

    ai::AiConnectionStatus status = env.service.TestConnection();
    EXPECT_EQ(status.state, ai::AiTaskState::Error);
    EXPECT_EQ(status.errorCode, ai::AiErrorCode::Disabled);
}

TEST(AiServiceTest, MissingKeyIsReportedWithoutProviderCall) {
    TempDir temp(MakeTempDir());
    TestAiEnvironment env(temp.path / "settings.json");
    ai::AiProviderSettings settings;
    settings.enabled = true;
    std::string error;
    ASSERT_TRUE(env.service.SaveSettings(settings, error)) << error;

    ai::AiConnectionStatus status = env.service.TestConnection();
    EXPECT_EQ(status.state, ai::AiTaskState::Error);
    EXPECT_EQ(status.errorCode, ai::AiErrorCode::MissingApiKey);
}

TEST(AiServiceTest, StoredKeyAllowsConnectionTest) {
    TempDir temp(MakeTempDir());
    TestAiEnvironment env(temp.path / "settings.json");
    ai::AiProviderSettings settings;
    settings.enabled = true;
    std::string error;
    ASSERT_TRUE(env.service.SaveSettings(settings, error)) << error;
    ASSERT_TRUE(env.service.SaveApiKey("sk-test").success);

    ai::AiConnectionStatus status = env.service.TestConnection();
    EXPECT_EQ(status.state, ai::AiTaskState::Success);
    EXPECT_TRUE(env.service.HasStoredApiKey());
}

TEST(AiServiceTest, DeleteApiKeyRemovesStoredKey) {
    TempDir temp(MakeTempDir());
    TestAiEnvironment env(temp.path / "settings.json");
    ASSERT_TRUE(env.service.SaveApiKey("sk-test").success);
    ASSERT_TRUE(env.service.HasStoredApiKey());

    ASSERT_TRUE(env.service.DeleteApiKey().success);
    EXPECT_FALSE(env.service.HasStoredApiKey());
}