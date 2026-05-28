#pragma once

#include "core/project_model.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace core {

std::expected<std::string, std::string>                ReadTextFile(const std::filesystem::path& path);
std::expected<void, std::string>                       WriteTextFile(const std::filesystem::path& path,
                                                                     std::string_view content);
// Crash-safe write: writes `content` to `<path>.tmp`, fsyncs that file, then
// atomically renames it over `path`. On Windows uses `MoveFileExW` with
// MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH; on POSIX uses
// `rename(2)` after `fsync(2)` of the temp file. Used for SACM autosave and
// manifest writes where a torn write would leave the project in an
// unrecoverable state.
std::expected<void, std::string>                       WriteTextFileAtomic(const std::filesystem::path& path,
                                                                           std::string_view content);
// Force-flush kernel buffers for `path` to disk. Returns false (and sets
// `error`) on I/O failure. Used by the event-store appender after each
// transaction to guarantee durability across power loss.
bool                                                   FsyncFile(const std::filesystem::path& path, std::string& error);
std::expected<std::vector<unsigned char>, std::string> ReadFileBytes(const std::filesystem::path& path);
// Sha256String currently cannot fail; returns the hex digest directly.
std::string                                            Sha256String(const std::string& content);
std::expected<std::string, std::string>                Sha256File(const std::filesystem::path& path);
bool                                                   IsSafeRelativePath(const std::filesystem::path& path);
void                                                   ComputeSacmHashes(ProjectFileEntry& entry,
                                                                         const std::filesystem::path& absolute_path);
std::expected<void, std::string>                       RefreshEntryHashes(AssuranceProject& project,
                                                                          ProjectFileEntry& entry,
                                                                          bool detect_external_change);

} // namespace core
