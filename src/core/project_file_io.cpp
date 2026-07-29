#include "core/project_file_io.h"

#include "core/sha256.h"
#include "parser/xml_parser.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace core {

std::expected<std::string, std::string> ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return std::unexpected("Could not open " + path.string());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof())
        return std::unexpected("Could not read " + path.string());
    return buffer.str();
}

std::expected<void, std::string> WriteTextFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return std::unexpected("Could not write " + path.string());
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good())
        return std::unexpected("Could not finish writing " + path.string());
    return {};
}

namespace {

#if defined(_WIN32)
// fsync-equivalent on Windows for a path: open with FILE_FLAG_WRITE_THROUGH
// is too restrictive (we want to fsync an already-written file), so we
// open the path with GENERIC_WRITE and call FlushFileBuffers.
bool FlushPathBuffers(const std::filesystem::path& path, std::string& error) {
    HANDLE h = CreateFileW(path.wstring().c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "Could not open for fsync: " + path.string() +
                " (err=" + std::to_string(GetLastError()) + ")";
        return false;
    }
    const BOOL ok = FlushFileBuffers(h);
    const DWORD last = ok ? 0u : GetLastError();
    CloseHandle(h);
    if (!ok) {
        error = "FlushFileBuffers failed for " + path.string() +
                " (err=" + std::to_string(last) + ")";
        return false;
    }
    return true;
}
#else
bool FlushPathBuffers(const std::filesystem::path& path, std::string& error) {
    const int fd = ::open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        error = "Could not open for fsync: " + path.string();
        return false;
    }
    const int rc = ::fsync(fd);
    ::close(fd);
    if (rc != 0) {
        error = "fsync failed for " + path.string();
        return false;
    }
    return true;
}
#endif

} // namespace

bool FsyncFile(const std::filesystem::path& path, std::string& error) {
    return FlushPathBuffers(path, error);
}

std::expected<void, std::string> WriteTextFileAtomic(const std::filesystem::path& path,
                                                     std::string_view content) {
    // Write to `<path>.tmp` in the same directory, fsync, then atomically
    // rename over `path`. Same-directory rename is required for atomicity on
    // most filesystems.
    std::filesystem::path tmp = path;
    tmp += ".tmp";

    // Best-effort cleanup of a stale `.tmp` from a prior crash; ignored on
    // failure because the open-for-write below will overwrite it.
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return std::unexpected("Could not open temp file " + tmp.string());
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();
        if (!file.good())
            return std::unexpected("Could not finish writing temp file " + tmp.string());
        // ofstream destructor closes the handle; on Windows the file must be
        // closed before MoveFileExW can replace `path`.
    }

    std::string fsync_err;
    if (!FsyncFile(tmp, fsync_err)) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(std::move(fsync_err));
    }

#if defined(_WIN32)
    if (!MoveFileExW(tmp.wstring().c_str(), path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD err = GetLastError();
        std::filesystem::remove(tmp, ec);
        return std::unexpected("Atomic rename failed for " + path.string() +
                               " (err=" + std::to_string(err) + ")");
    }
#else
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected("Atomic rename failed for " + path.string() + ": " + ec.message());
    }
#endif
    return {};
}

std::expected<std::vector<unsigned char>, std::string> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return std::unexpected("Could not open " + path.string());
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size < 0)
        return std::unexpected("Could not determine size of " + path.string());
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if (!bytes.empty())
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!file.good() && !file.eof())
        return std::unexpected("Could not read " + path.string());
    return bytes;
}

std::string Sha256String(const std::string& content) {
    return Sha256::HexDigest(content);
}

std::expected<std::string, std::string> Sha256File(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));
    return Sha256::HexDigest(*bytes);
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute())
        return false;
    for (const auto& part : path) {
        if (part == "..")
            return false;
    }
    return true;
}

void ComputeSacmHashes(ProjectFileEntry& entry, const std::filesystem::path& absolute_path) {
    auto result = parser::parse_sacm_xml(absolute_path.string());
    if (!result) {
        entry.parseStatus = "parseError";
        entry.state = ProjectFileState::ParseError;
        entry.lastError = std::move(result.error());
        entry.semanticHash.clear();
        entry.elementIndexHash.clear();
        entry.relationshipGraphHash.clear();
        return;
    }

    entry.parseStatus = "parsed";
    entry.lastError.clear();

    std::vector<std::string> semantic_lines;
    std::vector<std::string> element_ids;
    std::vector<std::string> relationship_lines;

    for (const auto& element : result->elements) {
        element_ids.push_back(element.id);
        std::string semantic_line =
            element.id + "|" + element.type + "|" + element.name + "|" + element.content + "|" +
            element.assertion_declaration;
        if (element.is_abstract)
            semantic_line += "|abstract";
        semantic_lines.push_back(std::move(semantic_line));
        if (!element.source_refs.empty() || !element.target_refs.empty()) {
            for (const auto& source : element.source_refs) {
                for (const auto& target : element.target_refs) {
                    relationship_lines.push_back(source + "|" + target + "|" + element.type + "|" +
                                                 element.reasoning_ref);
                }
            }
        }
    }

    auto join_and_hash = [](std::vector<std::string> lines, std::string& hash) {
        std::sort(lines.begin(), lines.end());
        std::ostringstream normalized;
        for (const auto& line : lines)
            normalized << line << '\n';
        hash = Sha256String(normalized.str());
    };

    join_and_hash(semantic_lines, entry.semanticHash);
    join_and_hash(element_ids, entry.elementIndexHash);
    join_and_hash(relationship_lines, entry.relationshipGraphHash);
}

std::expected<void, std::string>
RefreshEntryHashes(AssuranceProject& project, ProjectFileEntry& entry, bool detect_external_change) {
    if (!IsSafeRelativePath(entry.relativePath)) {
        entry.state = ProjectFileState::UnsupportedVersion;
        entry.lastError = "Tracked path is not a safe relative path";
        return {};
    }

    std::filesystem::path absolute_path = project.rootPath / entry.relativePath;
    std::error_code ec;
    if (!std::filesystem::exists(absolute_path, ec)) {
        entry.state = ProjectFileState::Missing;
        entry.lastError = "Tracked file is missing";
        return {};
    }

    std::string previous_raw_hash = entry.rawHash;
    auto raw_hash = Sha256File(absolute_path);
    if (!raw_hash)
        return std::unexpected(std::move(raw_hash.error()));
    entry.rawHash = std::move(*raw_hash);

    if (entry.role == ProjectFileRole::SacmArgument) {
        ComputeSacmHashes(entry, absolute_path);
    } else {
        entry.parseStatus = "notParsed";
        entry.lastError.clear();
    }

    if (entry.state != ProjectFileState::ParseError) {
        if (detect_external_change && !previous_raw_hash.empty() && previous_raw_hash != entry.rawHash) {
            entry.state = entry.role == ProjectFileRole::SacmArgument ? ProjectFileState::ModifiedButCompatible
                                                                      : ProjectFileState::ModifiedOutsideAssuranceForge;
        } else {
            entry.state = ProjectFileState::Clean;
        }
    }
    return {};
}

} // namespace core
