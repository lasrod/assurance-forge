// The platform secret store (issue #53).
//
// Until this landed, `CreatePlatformSecretStore()` returned a store that
// refused everything on anything that was not Windows, so the AI features did
// not work at all on macOS or Linux. macOS now uses Keychain Services and Linux
// uses libsecret when it is available at build time.
//
// What is testable here is bounded by what the platform provides. A round trip
// exercises the REAL store, so it runs only where one is reachable: Windows and
// macOS always have one, and a Linux CI runner or an ssh session usually has no
// keyring daemon at all. Rather than mock that away -- a mock of the keychain
// would test the mock -- the round trip is guarded on `IsAvailable()` and
// SKIPS visibly when there is nothing to talk to, so a green run is never
// mistaken for evidence it did not gather.

#include "ai/secret_store.h"

#include <gtest/gtest.h>

#include <string>

namespace {

// A service name of our own, so the test can never read or delete the user's
// real API key. Both fields are part of the item's identity on every platform.
constexpr const char* kTestService = "AssuranceForge.SecretStoreTest";
constexpr const char* kTestAccount = "test-account";

TEST(SecretStore, PlatformStoreExistsOnEveryPlatform) {
    // The factory must always hand back something. Returning null would make
    // every caller's null check the real error path.
    const std::shared_ptr<ai::ISecretStore> store = ai::CreatePlatformSecretStore();
    ASSERT_NE(store, nullptr);
}

// Windows (Credential Manager) and macOS (Keychain Services) both ship a store
// with the operating system, so "unavailable" there is a defect rather than a
// configuration. Linux has no such guarantee and is deliberately not asserted.
TEST(SecretStore, IsAvailableWhereThePlatformShipsAStore) {
    const std::shared_ptr<ai::ISecretStore> store = ai::CreatePlatformSecretStore();
    ASSERT_NE(store, nullptr);
#if defined(_WIN32) || defined(__APPLE__)
    EXPECT_TRUE(store->IsAvailable())
        << "this platform ships a credential store, so an unavailable one is a build or wiring defect";
#else
    SUCCEED() << "secure storage availability is not guaranteed on this platform: "
              << (store->IsAvailable() ? "a keyring was found" : "no keyring was found");
#endif
}

// An unavailable store has to say what to do about it, and the two ways to be
// unavailable have different answers. A user told to rebuild when their build
// is fine goes a long way in the wrong direction, and one told only
// "unavailable" concludes the feature does not exist on their platform.
//
//   backend "none"      -> the build has no keyring support: install and rebuild
//   backend "libsecret" -> the build is fine, nothing is running: start a keyring
//
// Which is why `SecretStoreBackendName()` exists: without it this test cannot
// tell which refusal it is looking at, and would have to accept either message
// for either cause.
TEST(SecretStore, RefusalNamesWhatIsMissing) {
    const std::shared_ptr<ai::ISecretStore> store = ai::CreatePlatformSecretStore();
    ASSERT_NE(store, nullptr);
    if (store->IsAvailable()) {
        GTEST_SKIP() << "a secret store is available here, so there is no refusal to inspect";
    }
    const ai::SecretStoreResult result = store->SaveSecret(kTestService, kTestAccount, "value");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, ai::AiErrorCode::SecureStoreUnavailable);
    ASSERT_FALSE(result.errorMessage.empty());

    const std::string backend = ai::SecretStoreBackendName();
    if (backend == "none") {
        EXPECT_NE(result.errorMessage.find("libsecret"), std::string::npos)
            << "this build has no keyring support, so the refusal must name the package that would add it: "
            << result.errorMessage;
    } else {
        EXPECT_NE(result.errorMessage.find("keyring"), std::string::npos)
            << "the build supports " << backend
            << " but nothing is running, and the refusal does not say so: " << result.errorMessage;
        EXPECT_EQ(result.errorMessage.find("rebuild"), std::string::npos)
            << "the build is fine; telling the user to rebuild sends them after the wrong problem: "
            << result.errorMessage;
    }
}

// The backend a build compiled in, which is a different question from whether
// it currently works. Reported so the settings UI and a bug report can tell a
// missing package from a missing daemon.
TEST(SecretStore, ReportsWhichBackendTheBuildContains) {
    const std::string backend = ai::SecretStoreBackendName();
#if defined(_WIN32)
    EXPECT_EQ(backend, "Windows Credential Manager");
#elif defined(__APPLE__)
    EXPECT_EQ(backend, "macOS Keychain");
#else
    EXPECT_TRUE(backend == "libsecret" || backend == "none") << backend;
#endif
    // An available store must have come from somewhere.
    const std::shared_ptr<ai::ISecretStore> store = ai::CreatePlatformSecretStore();
    ASSERT_NE(store, nullptr);
    if (store->IsAvailable()) {
        EXPECT_NE(backend, "none") << "a store reports itself available in a build with no backend";
    }
}

// Save, read back, overwrite, delete -- against the real platform store, which
// is the only thing that proves the Keychain and libsecret calls are right.
// Everything it writes is removed again, including on the failure paths.
TEST(SecretStore, RoundTripsThroughThePlatformStore) {
    const std::shared_ptr<ai::ISecretStore> store = ai::CreatePlatformSecretStore();
    ASSERT_NE(store, nullptr);
    if (!store->IsAvailable()) {
        GTEST_SKIP() << "no platform secret store is reachable here (no keyring daemon, or a build without one)";
    }
    // Any leftover from an interrupted earlier run would make the first read
    // below pass for the wrong reason.
    store->DeleteSecret(kTestService, kTestAccount);

    const ai::SecretLoadResult before = store->LoadSecret(kTestService, kTestAccount);
    EXPECT_TRUE(before.success) << before.errorMessage;
    EXPECT_FALSE(before.secret.has_value()) << "a key was already stored under the test service name";

    const std::string secret = "sk-test-000111222333";
    const ai::SecretStoreResult saved = store->SaveSecret(kTestService, kTestAccount, secret);
    ASSERT_TRUE(saved.success) << saved.errorMessage;

    const ai::SecretLoadResult loaded = store->LoadSecret(kTestService, kTestAccount);
    EXPECT_TRUE(loaded.success) << loaded.errorMessage;
    ASSERT_TRUE(loaded.secret.has_value());
    EXPECT_EQ(*loaded.secret, secret);

    // Re-saving must REPLACE. The Keychain's SecItemAdd refuses an existing
    // (service, account) rather than overwriting, so without the update path a
    // user who re-entered their key would silently keep the old one.
    const std::string replacement = "sk-test-444555666777";
    const ai::SecretStoreResult resaved = store->SaveSecret(kTestService, kTestAccount, replacement);
    ASSERT_TRUE(resaved.success) << resaved.errorMessage;
    const ai::SecretLoadResult reloaded = store->LoadSecret(kTestService, kTestAccount);
    EXPECT_TRUE(reloaded.success) << reloaded.errorMessage;
    ASSERT_TRUE(reloaded.secret.has_value());
    EXPECT_EQ(*reloaded.secret, replacement) << "re-saving kept the previous key instead of replacing it";

    const ai::SecretStoreResult deleted = store->DeleteSecret(kTestService, kTestAccount);
    EXPECT_TRUE(deleted.success) << deleted.errorMessage;

    const ai::SecretLoadResult after = store->LoadSecret(kTestService, kTestAccount);
    EXPECT_TRUE(after.success) << after.errorMessage;
    EXPECT_FALSE(after.secret.has_value()) << "the key survived deletion";

    // Deleting again is the caller's intended end state, not an error, and all
    // three platforms are expected to agree on that.
    const ai::SecretStoreResult deleted_again = store->DeleteSecret(kTestService, kTestAccount);
    EXPECT_TRUE(deleted_again.success) << deleted_again.errorMessage;
}

// An empty key is a user mistake with its own error code, not a storage
// failure -- the settings panel distinguishes them.
TEST(SecretStore, EmptySecretIsRejectedAsAMissingKey) {
    const std::shared_ptr<ai::ISecretStore> store = ai::CreatePlatformSecretStore();
    ASSERT_NE(store, nullptr);
    if (!store->IsAvailable()) {
        GTEST_SKIP() << "no platform secret store is reachable here";
    }
    const ai::SecretStoreResult result = store->SaveSecret(kTestService, kTestAccount, "");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, ai::AiErrorCode::MissingApiKey);
}

} // namespace
