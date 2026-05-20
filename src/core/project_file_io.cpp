#include "core/project_file_io.h"

#include "core/sha256.h"
#include "parser/xml_parser.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

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
        semantic_lines.push_back(element.id + "|" + element.type + "|" + element.name + "|" + element.content + "|" +
                                 element.assertion_declaration);
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
