#pragma once

#include "core/project_model.h"

#include <filesystem>
#include <string>
#include <vector>

namespace core {

std::string ReadTextFile(const std::filesystem::path& path, std::string& error);
bool WriteTextFile(const std::filesystem::path& path, const std::string& content, std::string& error);
bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes, std::string& error);
bool Sha256String(const std::string& content, std::string& hash, std::string& error);
bool Sha256File(const std::filesystem::path& path, std::string& hash, std::string& error);
bool IsSafeRelativePath(const std::filesystem::path& path);
void ComputeSacmHashes(ProjectFileEntry& entry, const std::filesystem::path& absolute_path);
bool RefreshEntryHashes(AssuranceProject& project,
                        ProjectFileEntry& entry,
                        bool detect_external_change,
                        std::string& error);

} // namespace core
