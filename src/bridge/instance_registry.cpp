#include "bridge/instance_registry.h"

#include "core/sha256.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#else
#include <cerrno>
#include <signal.h>
#endif

namespace bridge {
namespace {

// Runtime state, not configuration: it is meaningless after a reboot and must
// not be synced or backed up. Each platform has a directory for exactly that.
std::filesystem::path UserRuntimeDirectory() {
#ifdef _WIN32
    const char* local_appdata = std::getenv("LOCALAPPDATA");
    if (local_appdata != nullptr && *local_appdata != '\0') {
        return std::filesystem::path(local_appdata) / "AssuranceForge";
    }
#else
    // XDG_RUNTIME_DIR is the correct home for a socket: it is user-owned, mode
    // 0700, and cleared at logout. It is not guaranteed to exist, hence the
    // state-directory fallback.
    const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime != nullptr && *xdg_runtime != '\0') {
        return std::filesystem::path(xdg_runtime) / "assurance-forge";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "state" / "assurance-forge";
    }
#endif
    return std::filesystem::temp_directory_path() / "AssuranceForge";
}

std::filesystem::path BridgeDirectory() {
    return UserRuntimeDirectory() / "bridge";
}

std::string NormalizedProjectPath(const std::filesystem::path& project_root) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(project_root, ec);
    if (ec) {
        canonical = project_root;
    }
    std::string text = canonical.generic_string();
#ifdef _WIN32
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
#endif
    return text;
}

std::string RandomHex(int byte_count) {
    std::random_device device;
    std::ostringstream hex;
    static const char kDigits[] = "0123456789abcdef";
    for (int index = 0; index < byte_count; ++index) {
        const unsigned int byte = device() & 0xFFu;
        hex << kDigits[(byte >> 4) & 0x0Fu] << kDigits[byte & 0x0Fu];
    }
    return hex.str();
}

// The instance id becomes a filename and a transport address. Restricting it
// to this alphabet is what makes `RecordPathFor` unable to escape the
// instances directory, whatever a future caller passes.
bool IsSafeInstanceId(const std::string& instance_id) {
    return !instance_id.empty() &&
           instance_id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos;
}

std::filesystem::path RecordPathFor(const std::string& instance_id) {
    return InstancesDirectory() / (instance_id + ".json");
}

#ifdef _WIN32
// An explicit user-only DACL on the instances directory, inherited by the
// record files created inside it. The default DACL under %LOCALAPPDATA%
// already denies other interactive users, but the token in these files is
// what authorizes driving the application, so the directory states its access
// control itself instead of borrowing whatever the profile inherited.
// Best-effort: on failure the directory keeps the inherited default, which is
// the pre-existing behaviour, not an exposure this code introduces.
void ApplyUserOnlyAclBestEffort(const std::filesystem::path& directory) {
    HANDLE process_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token)) {
        return;
    }
    // Aligned as the struct the API fills it with; a bare BYTE buffer is
    // 1-aligned and the reinterpret_cast would be undefined behaviour.
    alignas(TOKEN_USER) BYTE user_buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE] = {};
    DWORD user_size = 0;
    const BOOL got_user = GetTokenInformation(process_token, TokenUser, user_buffer, sizeof(user_buffer), &user_size);
    CloseHandle(process_token);
    if (!got_user) {
        return;
    }
    PSID user_sid = reinterpret_cast<TOKEN_USER*>(user_buffer)->User.Sid;

    // The user and SYSTEM, full control, inheritable by contained files.
    // No Administrators entry on purpose: an elevated process can take
    // ownership anyway, and listing it here would only widen the everyday DACL.
    EXPLICIT_ACCESS_W access[2] = {};
    access[0].grfAccessPermissions = GENERIC_ALL;
    access[0].grfAccessMode = SET_ACCESS;
    access[0].grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    access[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    access[0].Trustee.ptstrName = reinterpret_cast<LPWSTR>(user_sid);

    alignas(SID) BYTE system_sid_buffer[SECURITY_MAX_SID_SIZE] = {};
    DWORD system_sid_size = sizeof(system_sid_buffer);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid_buffer, &system_sid_size)) {
        return;
    }
    access[1] = access[0];
    access[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access[1].Trustee.ptstrName = reinterpret_cast<LPWSTR>(system_sid_buffer);

    PACL acl = nullptr;
    if (SetEntriesInAclW(2, access, nullptr, &acl) != ERROR_SUCCESS) {
        return;
    }
    const std::wstring path = directory.wstring();
    SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
                          SE_FILE_OBJECT,
                          DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                          nullptr,
                          nullptr,
                          acl,
                          nullptr);
    LocalFree(acl);
}
#endif

bool ParseRecordFile(const std::filesystem::path& path, InstanceRecord& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    const nlohmann::json document = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return false;
    }

    out.protocol = document.value("protocol", 0);
    out.instance_id = document.value("instanceId", std::string());
    out.pid = document.value("pid", 0LL);
    out.address = document.value("address", std::string());
    out.token = document.value("token", std::string());
    out.app_version = document.value("appVersion", std::string());
    out.state = document.value("state", std::string());
    out.project_key = document.value("projectKey", std::string());
    out.last_heartbeat_utc = document.value("lastHeartbeatUtc", std::string());

    return !out.instance_id.empty() && !out.address.empty() && !out.token.empty();
}

} // namespace

std::string ProjectKey(const std::filesystem::path& project_root) {
    return core::Sha256::HexDigest(NormalizedProjectPath(project_root)).substr(0, 16);
}

std::filesystem::path InstancesDirectory() {
    return BridgeDirectory() / "instances";
}

std::string GenerateInstanceId() {
    return "af-" + RandomHex(8);
}

std::string InstanceAddress(const std::string& instance_id) {
#ifdef _WIN32
    return "\\\\.\\pipe\\assurance-forge-" + instance_id;
#else
    // The socket lives beside the instances directory, not inside it. The
    // whole address must fit `sun_path` (104 bytes on macOS), and with a long
    // home-directory fallback the extra "/instances" was measured to push a
    // real address to 105. Records are enumerated, sockets are not, so only
    // the records need the shared directory.
    return (BridgeDirectory() / (instance_id + ".sock")).string();
#endif
}

std::string GenerateToken() {
    return RandomHex(32);
}

bool WriteInstanceRecord(const InstanceRecord& record, std::string& error) {
    error.clear();
    if (!IsSafeInstanceId(record.instance_id)) {
        error = "An instance record needs an instance id of lowercase letters, digits and dashes.";
        return false;
    }
    const std::filesystem::path path = RecordPathFor(record.instance_id);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Could not create the bridge instances directory: " + ec.message();
        return false;
    }
#ifdef _WIN32
    ApplyUserOnlyAclBestEffort(path.parent_path());
#endif

    const nlohmann::json document{
        {"protocol", record.protocol},
        {"instanceId", record.instance_id},
        {"pid", record.pid},
        {"address", record.address},
        {"token", record.token},
        {"appVersion", record.app_version},
        {"state", record.state},
        {"projectKey", record.project_key},
        {"lastHeartbeatUtc", record.last_heartbeat_utc},
    };

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "Could not write the bridge instance record: " + path.string();
        return false;
    }
    file << document.dump(2);
    if (!file) {
        error = "Could not write the bridge instance record: " + path.string();
        return false;
    }
    file.close();

#ifndef _WIN32
    // The token is in this file. Anyone who can read it can drive the
    // application; the containing directory is already user-only, and this makes
    // the file itself say so too.
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 ec);
#endif
    return true;
}

void RemoveInstanceRecord(const std::string& instance_id) {
    if (!IsSafeInstanceId(instance_id)) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(RecordPathFor(instance_id), ec);
}

bool IsProcessAlive(long long pid) {
    if (pid <= 0) {
        return false;
    }
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        // Access denied still proves existence -- it is some other user's
        // process, which cannot be one of ours, so treat it as not-alive for
        // discovery purposes only when lookup failed for a different reason.
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    DWORD exit_code = 0;
    const BOOL got_code = GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    return got_code && exit_code == STILL_ACTIVE;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
#endif
}

std::vector<InstanceRecord> EnumerateInstanceRecords() {
    std::vector<InstanceRecord> records;
    std::error_code ec;
    std::filesystem::directory_iterator entries(InstancesDirectory(), ec);
    if (ec) {
        return records;
    }
    for (const std::filesystem::directory_entry& entry : entries) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") {
            continue;
        }
        InstanceRecord record;
        if (!ParseRecordFile(entry.path(), record)) {
            continue;
        }
        // The id inside the file must be the id the file is named for.
        // Removal and pruning delete by id, so a mismatched copy would either
        // survive pruning forever or delete a different instance's record.
        if (!IsSafeInstanceId(record.instance_id) || entry.path().stem().string() != record.instance_id) {
            continue;
        }
        records.push_back(std::move(record));
    }
    return records;
}

int PruneStaleInstanceRecords() {
    int pruned = 0;
    for (const InstanceRecord& record : EnumerateInstanceRecords()) {
        if (!IsProcessAlive(record.pid)) {
            RemoveInstanceRecord(record.instance_id);
            ++pruned;
        }
    }
    return pruned;
}

bool FindInstanceForProject(const std::filesystem::path& project_root, InstanceRecord& out, std::string& error) {
    error.clear();
    PruneStaleInstanceRecords();

    const std::string wanted_key = ProjectKey(project_root);
    for (InstanceRecord& record : EnumerateInstanceRecords()) {
        if (record.project_key != wanted_key || !IsProcessAlive(record.pid)) {
            continue;
        }
        out = std::move(record);
        return true;
    }
    error = "No running Assurance Forge has this project open.";
    return false;
}

bool AnotherInstanceHasProjectOpen(const std::filesystem::path& project_root,
                                   const std::string& own_instance_id,
                                   InstanceRecord& out) {
    const std::string wanted_key = ProjectKey(project_root);
    for (InstanceRecord& record : EnumerateInstanceRecords()) {
        if (record.instance_id == own_instance_id || record.project_key != wanted_key || !IsProcessAlive(record.pid)) {
            continue;
        }
        out = std::move(record);
        return true;
    }
    return false;
}

} // namespace bridge
