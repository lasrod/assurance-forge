#include "ai/secret_store.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(AF_HAVE_LIBSECRET)
#include <libsecret/secret.h>
#endif

#include <cstddef>
#include <string>
#include <utility>

namespace ai {
namespace {

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty())
        return {};
    int required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
        return {};
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), required);
    return wide;
}

std::wstring TargetName(const std::string& service, const std::string& account) {
    return Utf8ToWide(service + ":" + account);
}

class WindowsCredentialStore final : public ISecretStore {
public:
    bool IsAvailable() const override {
        return true;
    }

    SecretStoreResult
    SaveSecret(const std::string& service, const std::string& account, const std::string& secret) override {
        if (secret.empty()) {
            return SecretStoreFailure(AiErrorCode::MissingApiKey, "API key is empty.");
        }
        if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "API key is too large for secure storage.");
        }

        std::wstring target = TargetName(service, account);
        std::wstring user = Utf8ToWide(account);

        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(target.c_str());
        credential.UserName = const_cast<LPWSTR>(user.c_str());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));

        if (!CredWriteW(&credential, 0)) {
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable,
                                      "Could not save API key to Windows Credential Manager.");
        }
        return SecretStoreSuccess();
    }

    SecretLoadResult LoadSecret(const std::string& service, const std::string& account) override {
        std::wstring target = TargetName(service, account);
        PCREDENTIALW raw = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &raw)) {
            DWORD error = GetLastError();
            if (error == ERROR_NOT_FOUND || error == ERROR_NO_SUCH_LOGON_SESSION) {
                return SecretLoadSuccess(std::nullopt);
            }
            return SecretLoadFailure(AiErrorCode::SecureStoreUnavailable,
                                     "Could not read API key from Windows Credential Manager.");
        }

        std::string secret;
        if (raw->CredentialBlob && raw->CredentialBlobSize > 0) {
            const char* begin = reinterpret_cast<const char*>(raw->CredentialBlob);
            secret.assign(begin, begin + raw->CredentialBlobSize);
        }
        CredFree(raw);
        if (secret.empty())
            return SecretLoadSuccess(std::nullopt);
        return SecretLoadSuccess(secret);
    }

    SecretStoreResult DeleteSecret(const std::string& service, const std::string& account) override {
        std::wstring target = TargetName(service, account);
        if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
            DWORD error = GetLastError();
            if (error == ERROR_NOT_FOUND)
                return SecretStoreSuccess();
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable,
                                      "Could not remove API key from Windows Credential Manager.");
        }
        return SecretStoreSuccess();
    }
};
#elif defined(__APPLE__)
// Keychain Services (Security.framework). Part of every macOS install, so
// unlike the Linux path below there is nothing optional about it.
//
// The item is a generic password keyed by (service, account) -- the same two
// coordinates the Windows target name concatenates -- so a key saved by one
// build is found by the next.
class ScopedCFType {
public:
    explicit ScopedCFType(CFTypeRef ref = nullptr) : ref_(ref) {}
    ~ScopedCFType() {
        if (ref_ != nullptr) {
            CFRelease(ref_);
        }
    }
    ScopedCFType(const ScopedCFType&) = delete;
    ScopedCFType& operator=(const ScopedCFType&) = delete;

    CFTypeRef get() const {
        return ref_;
    }
    CFTypeRef* out() {
        return &ref_;
    }

private:
    CFTypeRef ref_ = nullptr;
};

CFStringRef MakeCFString(const std::string& value) {
    return CFStringCreateWithBytes(kCFAllocatorDefault,
                                   reinterpret_cast<const UInt8*>(value.data()),
                                   static_cast<CFIndex>(value.size()),
                                   kCFStringEncodingUTF8,
                                   false);
}

class KeychainSecretStore final : public ISecretStore {
public:
    bool IsAvailable() const override {
        return true;
    }

    SecretStoreResult
    SaveSecret(const std::string& service, const std::string& account, const std::string& secret) override {
        if (secret.empty()) {
            return SecretStoreFailure(AiErrorCode::MissingApiKey, "API key is empty.");
        }
        ScopedCFType service_ref(MakeCFString(service));
        ScopedCFType account_ref(MakeCFString(account));
        ScopedCFType secret_ref(CFDataCreate(
            kCFAllocatorDefault, reinterpret_cast<const UInt8*>(secret.data()), static_cast<CFIndex>(secret.size())));
        if (service_ref.get() == nullptr || account_ref.get() == nullptr || secret_ref.get() == nullptr) {
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "Could not encode the API key.");
        }

        const void* query_keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* query_values[] = {kSecClassGenericPassword, service_ref.get(), account_ref.get()};
        ScopedCFType query(CFDictionaryCreate(kCFAllocatorDefault,
                                              query_keys,
                                              query_values,
                                              3,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks));

        // Update in place when the item already exists. SecItemAdd on an
        // existing (service, account) fails with errSecDuplicateItem rather
        // than overwriting, so a plain add would make re-entering a key
        // silently keep the old one.
        const void* update_keys[] = {kSecValueData};
        const void* update_values[] = {secret_ref.get()};
        ScopedCFType update(CFDictionaryCreate(kCFAllocatorDefault,
                                               update_keys,
                                               update_values,
                                               1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks));
        const OSStatus updated =
            SecItemUpdate(static_cast<CFDictionaryRef>(query.get()), static_cast<CFDictionaryRef>(update.get()));
        if (updated == errSecSuccess) {
            return SecretStoreSuccess();
        }
        if (updated != errSecItemNotFound) {
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable,
                                      "Could not update the API key in the macOS keychain.");
        }

        const void* add_keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
        const void* add_values[] = {kSecClassGenericPassword, service_ref.get(), account_ref.get(), secret_ref.get()};
        ScopedCFType add(CFDictionaryCreate(kCFAllocatorDefault,
                                            add_keys,
                                            add_values,
                                            4,
                                            &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks));
        if (SecItemAdd(static_cast<CFDictionaryRef>(add.get()), nullptr) != errSecSuccess) {
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable,
                                      "Could not save the API key to the macOS keychain.");
        }
        return SecretStoreSuccess();
    }

    SecretLoadResult LoadSecret(const std::string& service, const std::string& account) override {
        ScopedCFType service_ref(MakeCFString(service));
        ScopedCFType account_ref(MakeCFString(account));
        if (service_ref.get() == nullptr || account_ref.get() == nullptr) {
            return SecretLoadFailure(AiErrorCode::SecureStoreUnavailable, "Could not encode the keychain query.");
        }
        const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
        const void* values[] = {
            kSecClassGenericPassword, service_ref.get(), account_ref.get(), kCFBooleanTrue, kSecMatchLimitOne};
        ScopedCFType query(CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));

        ScopedCFType found;
        const OSStatus status = SecItemCopyMatching(static_cast<CFDictionaryRef>(query.get()), found.out());
        if (status == errSecItemNotFound) {
            return SecretLoadSuccess(std::nullopt);
        }
        if (status != errSecSuccess || found.get() == nullptr) {
            return SecretLoadFailure(AiErrorCode::SecureStoreUnavailable,
                                     "Could not read the API key from the macOS keychain.");
        }
        CFDataRef data = static_cast<CFDataRef>(found.get());
        const std::string secret(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                                 static_cast<std::size_t>(CFDataGetLength(data)));
        if (secret.empty()) {
            return SecretLoadSuccess(std::nullopt);
        }
        return SecretLoadSuccess(secret);
    }

    SecretStoreResult DeleteSecret(const std::string& service, const std::string& account) override {
        ScopedCFType service_ref(MakeCFString(service));
        ScopedCFType account_ref(MakeCFString(account));
        if (service_ref.get() == nullptr || account_ref.get() == nullptr) {
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "Could not encode the keychain query.");
        }
        const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
        const void* values[] = {kSecClassGenericPassword, service_ref.get(), account_ref.get()};
        ScopedCFType query(CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        const OSStatus status = SecItemDelete(static_cast<CFDictionaryRef>(query.get()));
        // Deleting what is not there is the caller's intended end state, and
        // the Windows path treats it the same way.
        if (status == errSecSuccess || status == errSecItemNotFound) {
            return SecretStoreSuccess();
        }
        return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable,
                                  "Could not remove the API key from the macOS keychain.");
    }
};
#elif defined(AF_HAVE_LIBSECRET)
// Freedesktop Secret Service via libsecret -- GNOME Keyring, KWallet and
// anything else implementing the D-Bus API. Optional at build time: when
// libsecret is not found, CMake leaves AF_HAVE_LIBSECRET undefined and the
// unsupported store below is compiled instead.
//
// There is deliberately NO file-based fallback. A plaintext or
// obfuscated-on-disk key that the user believes is in a keyring is worse than
// an honest refusal, and this project's rule everywhere else is to refuse
// visibly rather than degrade silently.
// The trailing reserved members are left to aggregate zero-initialization
// rather than spelled out: libsecret documents them as private, and counting
// them by hand is a way to get it silently wrong when the struct grows. The
// build already sets -Wno-missing-field-initializers for exactly this shape.
const SecretSchema* AssuranceForgeSchema() {
    static const SecretSchema schema = {
        "org.assuranceforge.ApiKey",
        SECRET_SCHEMA_NONE,
        {
            {"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
    };
    return &schema;
}

class LibsecretStore final : public ISecretStore {
public:
    bool IsAvailable() const override {
        // Probed once: libsecret being linked says nothing about a keyring
        // daemon being reachable, and a headless session (CI, ssh, a minimal
        // container) commonly has none. Reporting "available" there would make
        // the UI offer secure storage that then fails on save.
        static const bool available = [] {
            GError* error = nullptr;
            SecretService* service = secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &error);
            if (error != nullptr) {
                g_error_free(error);
                return false;
            }
            if (service == nullptr) {
                return false;
            }
            g_object_unref(service);
            return true;
        }();
        return available;
    }

    SecretStoreResult
    SaveSecret(const std::string& service, const std::string& account, const std::string& secret) override {
        if (secret.empty()) {
            return SecretStoreFailure(AiErrorCode::MissingApiKey, "API key is empty.");
        }
        const std::string label = service + ": " + account;
        GError* error = nullptr;
        const gboolean stored = secret_password_store_sync(AssuranceForgeSchema(),
                                                           SECRET_COLLECTION_DEFAULT,
                                                           label.c_str(),
                                                           secret.c_str(),
                                                           nullptr,
                                                           &error,
                                                           "service",
                                                           service.c_str(),
                                                           "account",
                                                           account.c_str(),
                                                           nullptr);
        if (error != nullptr) {
            SecretStoreResult failure =
                SecretStoreFailure(AiErrorCode::SecureStoreUnavailable,
                                   std::string("Could not save the API key to the keyring: ") + error->message);
            g_error_free(error);
            return failure;
        }
        if (stored == FALSE) {
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable,
                                      "Could not save the API key to the keyring.");
        }
        return SecretStoreSuccess();
    }

    SecretLoadResult LoadSecret(const std::string& service, const std::string& account) override {
        GError* error = nullptr;
        gchar* raw = secret_password_lookup_sync(
            AssuranceForgeSchema(), nullptr, &error, "service", service.c_str(), "account", account.c_str(), nullptr);
        if (error != nullptr) {
            SecretLoadResult failure =
                SecretLoadFailure(AiErrorCode::SecureStoreUnavailable,
                                  std::string("Could not read the API key from the keyring: ") + error->message);
            g_error_free(error);
            return failure;
        }
        if (raw == nullptr) {
            return SecretLoadSuccess(std::nullopt);
        }
        const std::string secret(raw);
        // Wipes the copy libsecret allocated rather than plain free().
        secret_password_free(raw);
        if (secret.empty()) {
            return SecretLoadSuccess(std::nullopt);
        }
        return SecretLoadSuccess(secret);
    }

    SecretStoreResult DeleteSecret(const std::string& service, const std::string& account) override {
        GError* error = nullptr;
        secret_password_clear_sync(
            AssuranceForgeSchema(), nullptr, &error, "service", service.c_str(), "account", account.c_str(), nullptr);
        if (error != nullptr) {
            SecretStoreResult failure =
                SecretStoreFailure(AiErrorCode::SecureStoreUnavailable,
                                   std::string("Could not remove the API key from the keyring: ") + error->message);
            g_error_free(error);
            return failure;
        }
        // A false return with no error means there was nothing to remove, which
        // is the caller's intended end state.
        return SecretStoreSuccess();
    }
};
#else
class UnsupportedSecretStore final : public ISecretStore {
public:
    bool IsAvailable() const override {
        return false;
    }

    SecretStoreResult SaveSecret(const std::string&, const std::string&, const std::string&) override {
        return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, UnavailableReason());
    }

    SecretLoadResult LoadSecret(const std::string&, const std::string&) override {
        return SecretLoadFailure(AiErrorCode::SecureStoreUnavailable, UnavailableReason());
    }

    SecretStoreResult DeleteSecret(const std::string&, const std::string&) override {
        return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, UnavailableReason());
    }

private:
    // Naming the missing package is the difference between a user installing it
    // and a user concluding the feature does not exist on their platform. This
    // build reaches here only on a Unix that is not macOS, since Windows and
    // macOS both have a store in the box.
    static std::string UnavailableReason() {
        return "Secure storage is unavailable: this build has no keyring support. Install libsecret "
               "(libsecret-1-dev on Debian/Ubuntu, libsecret-devel on Fedora) and rebuild.";
    }
};
#endif

} // namespace

SecretStoreResult SecretStoreSuccess() {
    SecretStoreResult result;
    result.success = true;
    result.errorCode = AiErrorCode::None;
    return result;
}

SecretStoreResult SecretStoreFailure(AiErrorCode errorCode, std::string message) {
    SecretStoreResult result;
    result.success = false;
    result.errorCode = errorCode;
    result.errorMessage = std::move(message);
    return result;
}

SecretLoadResult SecretLoadSuccess(std::optional<std::string> secret) {
    SecretLoadResult result;
    result.success = true;
    result.secret = std::move(secret);
    result.errorCode = AiErrorCode::None;
    return result;
}

SecretLoadResult SecretLoadFailure(AiErrorCode errorCode, std::string message) {
    SecretLoadResult result;
    result.success = false;
    result.errorCode = errorCode;
    result.errorMessage = std::move(message);
    return result;
}

std::shared_ptr<ISecretStore> CreatePlatformSecretStore() {
#ifdef _WIN32
    return std::make_shared<WindowsCredentialStore>();
#elif defined(__APPLE__)
    return std::make_shared<KeychainSecretStore>();
#elif defined(AF_HAVE_LIBSECRET)
    return std::make_shared<LibsecretStore>();
#else
    return std::make_shared<UnsupportedSecretStore>();
#endif
}

} // namespace ai
