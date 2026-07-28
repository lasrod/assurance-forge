#include "bridge/endpoint.h"

#include "core/sha256.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

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
    std::error_code       ec;
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

} // namespace

std::string ProjectKey(const std::filesystem::path& project_root) {
    // Half a sha256 is far more than enough to keep two project paths on one
    // machine apart, and a short name matters: a Windows pipe name and a POSIX
    // socket path both have length limits (`sun_path` is famously ~108 bytes).
    return core::Sha256::HexDigest(NormalizedProjectPath(project_root)).substr(0, 16);
}

std::filesystem::path EndpointRecordPath(const std::filesystem::path& project_root) {
    return BridgeDirectory() / (ProjectKey(project_root) + ".json");
}

std::string EndpointAddressFor(const std::filesystem::path& project_root) {
    const std::string key = ProjectKey(project_root);
#ifdef _WIN32
    return "\\\\.\\pipe\\assurance-forge-" + key;
#else
    return (BridgeDirectory() / (key + ".sock")).string();
#endif
}

std::string GenerateToken() {
    std::random_device      device;
    std::ostringstream      hex;
    constexpr int           kTokenBytes = 32;
    static const char       kDigits[]   = "0123456789abcdef";
    for (int index = 0; index < kTokenBytes; ++index) {
        const unsigned int byte = device() & 0xFFu;
        hex << kDigits[(byte >> 4) & 0x0Fu] << kDigits[byte & 0x0Fu];
    }
    return hex.str();
}

bool WriteEndpointRecord(const EndpointRecord& record, std::string& error) {
    error.clear();
    const std::filesystem::path path = EndpointRecordPath(record.project_root);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Could not create the bridge directory: " + ec.message();
        return false;
    }

    const nlohmann::json document{
        {"protocol", record.protocol},   {"pid", record.pid},
        {"address", record.address},     {"token", record.token},
        {"projectRoot", record.project_root}, {"appVersion", record.app_version},
    };

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "Could not write the bridge endpoint record: " + path.string();
        return false;
    }
    file << document.dump(2);
    if (!file) {
        error = "Could not write the bridge endpoint record: " + path.string();
        return false;
    }
    file.close();

#ifndef _WIN32
    // The token is in this file. Anyone who can read it can drive the
    // application; the containing directory is already user-only, and this makes
    // the file itself say so too.
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
#endif
    return true;
}

bool ReadEndpointRecord(const std::filesystem::path& project_root, EndpointRecord& out,
                        std::string& error) {
    error.clear();
    const std::filesystem::path path = EndpointRecordPath(project_root);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "No running Assurance Forge has this project open.";
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    const nlohmann::json document = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        error = "The bridge endpoint record is not readable: " + path.string();
        return false;
    }

    out.protocol     = document.value("protocol", 0);
    out.pid          = document.value("pid", 0LL);
    out.address      = document.value("address", std::string());
    out.token        = document.value("token", std::string());
    out.project_root = document.value("projectRoot", std::string());
    out.app_version  = document.value("appVersion", std::string());

    if (out.address.empty() || out.token.empty()) {
        error = "The bridge endpoint record is incomplete: " + path.string();
        return false;
    }
    return true;
}

void RemoveEndpointRecord(const std::filesystem::path& project_root) {
    std::error_code ec;
    std::filesystem::remove(EndpointRecordPath(project_root), ec);
}

} // namespace bridge
