#include "bridge/instance_registry.h"

#include "core/sha256.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
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
    // Once per process: the directory's ACL cannot change between writes, and
    // the record is rewritten on every heartbeat -- restamping NTFS security
    // metadata each time bought nothing.
    static std::once_flag acl_once;
    std::call_once(acl_once, [&path] { ApplyUserOnlyAclBestEffort(path.parent_path()); });
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

    // Temp-then-rename, because the record is rewritten in place on every
    // heartbeat while the adapter enumerates on every call: a truncate-and-
    // stream write has a window in which a reader parses half a document,
    // skips the record, and spuriously reports the application absent.
    const std::filesystem::path temp = path.parent_path() / (record.instance_id + ".tmp");
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "Could not write the bridge instance record: " + temp.string();
            return false;
        }
        file << document.dump(2);
        file.close();
        if (!file) {
            error = "Could not write the bridge instance record: " + temp.string();
            return false;
        }
    }

#ifndef _WIN32
    // The token is in this file. Anyone who can read it can drive the
    // application; the containing directory is already user-only, and this makes
    // the file itself say so too. Applied before the rename so the readable
    // window never exists.
    std::filesystem::permissions(temp,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 ec);
#endif

    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        error = "Could not publish the bridge instance record: " + path.string();
        return false;
    }
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
        // Access denied proves a process exists under that pid -- but not
        // ours: records are written per user, and a pid we cannot even query
        // is another user's or the system's, i.e. a recycled pid after our
        // instance died. Treating it as alive would leave a ghost record that
        // bricks discovery ("more than one instance") until someone deletes
        // the file by hand.
        return false;
    }
    DWORD exit_code = 0;
    const BOOL got_code = GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    return got_code && exit_code == STILL_ACTIVE;
#else
    // EPERM proves a process exists under that pid, but one we may not signal
    // -- another user's, so not ours. Same reasoning as the Windows branch.
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
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

namespace {

// A crashed instance leaves its record and, on POSIX, its socket file behind
// -- a random per-run address is never rebound, so nothing else would ever
// reclaim it. The address delete is confined to paths the registry itself
// would have chosen: a hostile record must not be able to aim it elsewhere.
void RemoveDeadInstance(const InstanceRecord& record) {
    std::error_code ec;
    const std::filesystem::path address(record.address);
    if (address.parent_path() == BridgeDirectory()) {
        std::filesystem::remove(address, ec);
    }
    RemoveInstanceRecord(record.instance_id);
}

} // namespace

std::vector<InstanceRecord> EnumerateLiveInstanceRecords() {
    // One pass owns the whole liveness policy: a record whose process is gone
    // is pruned as it is met, and only survivors are returned. Discovery runs
    // on every MCP call while disconnected, so one directory scan, one parse
    // and one liveness check per record is also the cheap form.
    std::vector<InstanceRecord> live;
    for (InstanceRecord& record : EnumerateInstanceRecords()) {
        if (IsProcessAlive(record.pid)) {
            live.push_back(std::move(record));
        } else {
            RemoveDeadInstance(record);
        }
    }
    return live;
}

int PruneStaleInstanceRecords() {
    int pruned = 0;
    for (const InstanceRecord& record : EnumerateInstanceRecords()) {
        if (!IsProcessAlive(record.pid)) {
            RemoveDeadInstance(record);
            ++pruned;
        }
    }
    return pruned;
}

bool FindInstanceForProject(const std::filesystem::path& project_root, InstanceRecord& out, std::string& error) {
    return FindInstanceForProjectKey(ProjectKey(project_root), out, error);
}

bool FindInstanceForProjectKey(const std::string& project_key, InstanceRecord& out, std::string& error) {
    error.clear();
    bool found = false;
    for (InstanceRecord& record : EnumerateLiveInstanceRecords()) {
        if (record.project_key != project_key) {
            continue;
        }
        if (found) {
            // Two instances have this project open. Picking one by directory
            // order would put an agent's draft groups in whichever window the
            // enumeration happened to yield -- possibly the one the user is
            // not looking at.
            error = "More than one running Assurance Forge has this project open. Close one of them; "
                    "this session never picks between instances by itself.";
            return false;
        }
        out = std::move(record);
        found = true;
    }
    if (!found) {
        error = "No running Assurance Forge has this project open.";
    }
    return found;
}

bool AnotherInstanceHasProjectOpen(const std::filesystem::path& project_root,
                                   const std::string& own_instance_id,
                                   InstanceRecord& out) {
    const std::string wanted_key = ProjectKey(project_root);
    for (InstanceRecord& record : EnumerateLiveInstanceRecords()) {
        if (record.instance_id == own_instance_id || record.project_key != wanted_key) {
            continue;
        }
        out = std::move(record);
        return true;
    }
    return false;
}

} // namespace bridge
