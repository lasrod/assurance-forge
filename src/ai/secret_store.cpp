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
#endif

#include <utility>

namespace ai {
namespace {

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), required);
    return wide;
}

std::wstring TargetName(const std::string& service, const std::string& account) {
    return Utf8ToWide(service + ":" + account);
}

class WindowsCredentialStore final : public ISecretStore {
public:
    bool IsAvailable() const override { return true; }

    SecretStoreResult SaveSecret(const std::string& service,
                                 const std::string& account,
                                 const std::string& secret) override {
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
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "Could not save API key to Windows Credential Manager.");
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
            return SecretLoadFailure(AiErrorCode::SecureStoreUnavailable, "Could not read API key from Windows Credential Manager.");
        }

        std::string secret;
        if (raw->CredentialBlob && raw->CredentialBlobSize > 0) {
            const char* begin = reinterpret_cast<const char*>(raw->CredentialBlob);
            secret.assign(begin, begin + raw->CredentialBlobSize);
        }
        CredFree(raw);
        if (secret.empty()) return SecretLoadSuccess(std::nullopt);
        return SecretLoadSuccess(secret);
    }

    SecretStoreResult DeleteSecret(const std::string& service, const std::string& account) override {
        std::wstring target = TargetName(service, account);
        if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
            DWORD error = GetLastError();
            if (error == ERROR_NOT_FOUND) return SecretStoreSuccess();
            return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "Could not remove API key from Windows Credential Manager.");
        }
        return SecretStoreSuccess();
    }
};
#else
class UnsupportedSecretStore final : public ISecretStore {
public:
    bool IsAvailable() const override { return false; }

    SecretStoreResult SaveSecret(const std::string&, const std::string&, const std::string&) override {
        return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "Secure storage is unavailable on this platform.");
    }

    SecretLoadResult LoadSecret(const std::string&, const std::string&) override {
        return SecretLoadFailure(AiErrorCode::SecureStoreUnavailable, "Secure storage is unavailable on this platform.");
    }

    SecretStoreResult DeleteSecret(const std::string&, const std::string&) override {
        return SecretStoreFailure(AiErrorCode::SecureStoreUnavailable, "Secure storage is unavailable on this platform.");
    }
};
#endif

}  // namespace

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
#else
    return std::make_shared<UnsupportedSecretStore>();
#endif
}

}  // namespace ai
