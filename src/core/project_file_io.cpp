#include "core/project_file_io.h"

#include "core/sha256.h"
#include "parser/xml_parser.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace core {

std::string ReadTextFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Could not open " + path.string();
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        error = "Could not read " + path.string();
        return {};
    }
    return buffer.str();
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& content, std::string& error) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "Could not write " + path.string();
        return false;
    }
    file << content;
    if (!file.good()) {
        error = "Could not finish writing " + path.string();
        return false;
    }
    return true;
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Could not open " + path.string();
        return false;
    }
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size < 0) {
        error = "Could not determine size of " + path.string();
        return false;
    }
    file.seekg(0, std::ios::beg);
    bytes.resize(static_cast<size_t>(size));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    }
    if (!file.good() && !file.eof()) {
        error = "Could not read " + path.string();
        return false;
    }
    return true;
}

bool Sha256String(const std::string& content, std::string& hash, std::string& error) {
    error.clear();
    hash = Sha256::HexDigest(content);
    return true;
}

bool Sha256File(const std::filesystem::path& path, std::string& hash, std::string& error) {
    std::vector<unsigned char> bytes;
    if (!ReadFileBytes(path, bytes, error))
        return false;
    error.clear();
    hash = Sha256::HexDigest(bytes);
    return true;
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
    if (!result.success) {
        entry.parseStatus = "parseError";
        entry.state = ProjectFileState::ParseError;
        entry.lastError = result.error_message;
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

    for (const auto& element : result.assurance_case.elements) {
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

    auto join_and_hash = [&entry](std::vector<std::string> lines, std::string& hash) -> bool {
        std::sort(lines.begin(), lines.end());
        std::ostringstream normalized;
        for (const auto& line : lines)
            normalized << line << '\n';
        std::string hash_error;
        if (!Sha256String(normalized.str(), hash, hash_error)) {
            entry.state = ProjectFileState::ParseError;
            entry.lastError = "Hash computation failed: " + hash_error;
            return false;
        }
        return true;
    };

    if (!join_and_hash(semantic_lines, entry.semanticHash))
        return;
    if (!join_and_hash(element_ids, entry.elementIndexHash))
        return;
    if (!join_and_hash(relationship_lines, entry.relationshipGraphHash))
        return;
}

bool RefreshEntryHashes(AssuranceProject& project,
                        ProjectFileEntry& entry,
                        bool detect_external_change,
                        std::string& error) {
    if (!IsSafeRelativePath(entry.relativePath)) {
        entry.state = ProjectFileState::UnsupportedVersion;
        entry.lastError = "Tracked path is not a safe relative path";
        return true;
    }

    std::filesystem::path absolute_path = project.rootPath / entry.relativePath;
    std::error_code ec;
    if (!std::filesystem::exists(absolute_path, ec)) {
        entry.state = ProjectFileState::Missing;
        entry.lastError = "Tracked file is missing";
        return true;
    }

    std::string previous_raw_hash = entry.rawHash;
    if (!Sha256File(absolute_path, entry.rawHash, error))
        return false;

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
    return true;
}

} // namespace core
