#include "core/project_service.h"

#include "parser/xml_parser.h"

#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace core {
namespace {

constexpr const char* kProjectFormat = "assurance-forge-project";
constexpr const char* kProjectFormatVersion = "0.1.0";
constexpr const char* kManifestFileName = "af.proj";

const std::array<const char*, 8> kProjectDirectories = {
    "arguments",
    "registers",
    "conformance",
    "exports",
    ".af/cache",
    ".af/backups",
    ".af/snapshots",
    ".af/history",
};

std::string Trim(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    return std::string(begin, end);
}

std::string NowUtc() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &time);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string BackupTimestampUtc() {
    auto timestamp = NowUtc();
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), '-'), timestamp.end());
    timestamp.erase(std::remove(timestamp.begin(), timestamp.end(), ':'), timestamp.end());
    return timestamp;
}

std::string GenerateId(const char* prefix) {
    static std::mt19937_64 rng{std::random_device{}()};
    static unsigned long long counter = 0;
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << prefix << "-" << std::hex << now << "-" << ++counter << "-" << rng();
    return out.str();
}

std::string EscapeJson(const std::string& value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c));
                } else {
                    out << c;
                }
                break;
        }
    }
    return out.str();
}

std::string Quote(const std::string& value) {
    return "\"" + EscapeJson(value) + "\"";
}

std::string EscapeXmlAttribute(const std::string& value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
            case '&': out << "&amp;"; break;
            case '"': out << "&quot;"; break;
            case '\'': out << "&apos;"; break;
            case '<': out << "&lt;"; break;
            case '>': out << "&gt;"; break;
            default: out << c; break;
        }
    }
    return out.str();
}

std::string ToGenericRelativePath(const std::filesystem::path& path) {
    return path.generic_string();
}

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

std::string HexString(const std::vector<unsigned char>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

bool Sha256Bytes(const unsigned char* data, size_t size, std::string& hash, std::string& error) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash_handle = nullptr;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        error = "Could not open SHA-256 provider";
        return false;
    }

    DWORD object_length = 0;
    DWORD result_length = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
                          &result_length, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        error = "Could not read SHA-256 object length";
        return false;
    }

    DWORD hash_length = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hash_length), sizeof(hash_length),
                          &result_length, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        error = "Could not read SHA-256 hash length";
        return false;
    }

    std::vector<unsigned char> object_buffer(object_length);
    std::vector<unsigned char> hash_buffer(hash_length);

    bool ok = true;
    if (BCryptCreateHash(algorithm, &hash_handle, object_buffer.data(), object_length, nullptr, 0, 0) != 0) {
        error = "Could not create SHA-256 hash";
        ok = false;
    } else if (BCryptHashData(hash_handle, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) != 0) {
        error = "Could not update SHA-256 hash";
        ok = false;
    } else if (BCryptFinishHash(hash_handle, hash_buffer.data(), hash_length, 0) != 0) {
        error = "Could not finish SHA-256 hash";
        ok = false;
    }

    if (hash_handle) BCryptDestroyHash(hash_handle);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    if (!ok) return false;
    hash = HexString(hash_buffer);
    return true;
}

bool Sha256String(const std::string& content, std::string& hash, std::string& error) {
    return Sha256Bytes(reinterpret_cast<const unsigned char*>(content.data()), content.size(), hash, error);
}

bool Sha256File(const std::filesystem::path& path, std::string& hash, std::string& error) {
    std::vector<unsigned char> bytes;
    if (!ReadFileBytes(path, bytes, error)) return false;
    return Sha256Bytes(bytes.data(), bytes.size(), hash, error);
}

size_t FindMatching(const std::string& text, size_t open_pos, char open_char, char close_char) {
    bool in_string = false;
    bool escape = false;
    int depth = 0;
    for (size_t i = open_pos; i < text.size(); ++i) {
        char c = text[i];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
        } else if (c == open_char) {
            ++depth;
        } else if (c == close_char) {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

size_t FindJsonKey(const std::string& object, const std::string& key) {
    return object.find(Quote(key));
}

bool ParseJsonStringAt(const std::string& text, size_t quote_pos, std::string& value) {
    if (quote_pos == std::string::npos || quote_pos >= text.size() || text[quote_pos] != '"') return false;
    value.clear();
    bool escape = false;
    for (size_t i = quote_pos + 1; i < text.size(); ++i) {
        char c = text[i];
        if (escape) {
            switch (c) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(c); break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            return true;
        } else {
            value.push_back(c);
        }
    }
    return false;
}

std::string JsonStringValue(const std::string& object, const std::string& key, const std::string& fallback = {}) {
    size_t key_pos = FindJsonKey(object, key);
    if (key_pos == std::string::npos) return fallback;
    size_t colon = object.find(':', key_pos);
    if (colon == std::string::npos) return fallback;
    size_t quote = object.find('"', colon + 1);
    std::string value;
    if (!ParseJsonStringAt(object, quote, value)) return fallback;
    return value;
}

std::string JsonObjectSection(const std::string& object, const std::string& key) {
    size_t key_pos = FindJsonKey(object, key);
    if (key_pos == std::string::npos) return {};
    size_t open = object.find('{', key_pos);
    if (open == std::string::npos) return {};
    size_t close = FindMatching(object, open, '{', '}');
    if (close == std::string::npos) return {};
    return object.substr(open, close - open + 1);
}

std::string JsonArraySection(const std::string& object, const std::string& key) {
    size_t key_pos = FindJsonKey(object, key);
    if (key_pos == std::string::npos) return {};
    size_t open = object.find('[', key_pos);
    if (open == std::string::npos) return {};
    size_t close = FindMatching(object, open, '[', ']');
    if (close == std::string::npos) return {};
    return object.substr(open, close - open + 1);
}

std::vector<std::string> TopLevelObjectsInArray(const std::string& array_text) {
    std::vector<std::string> objects;
    size_t pos = 0;
    while (true) {
        size_t open = array_text.find('{', pos);
        if (open == std::string::npos) break;
        size_t close = FindMatching(array_text, open, '{', '}');
        if (close == std::string::npos) break;
        objects.push_back(array_text.substr(open, close - open + 1));
        pos = close + 1;
    }
    return objects;
}

std::string SerializeManifest(const AssuranceProject& project) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"format\": \"" << kProjectFormat << "\",\n";
    out << "  \"formatVersion\": " << Quote(project.formatVersion) << ",\n";
    out << "  \"project\": {\n";
    out << "    \"id\": " << Quote(project.id) << ",\n";
    out << "    \"name\": " << Quote(project.name) << ",\n";
    out << "    \"description\": " << Quote(project.description) << ",\n";
    out << "    \"createdUtc\": " << Quote(project.createdUtc) << ",\n";
    out << "    \"modifiedUtc\": " << Quote(project.modifiedUtc) << "\n";
    out << "  },\n";
    out << "  \"tool\": {\n";
    out << "    \"createdWith\": " << Quote(project.createdWith) << ",\n";
    out << "    \"lastOpenedWith\": " << Quote(project.lastOpenedWith) << "\n";
    out << "  },\n";
    out << "  \"files\": [";
    if (!project.files.empty()) out << "\n";
    for (size_t i = 0; i < project.files.size(); ++i) {
        const auto& file = project.files[i];
        out << "    {\n";
        out << "      \"id\": " << Quote(file.id) << ",\n";
        out << "      \"path\": " << Quote(ToGenericRelativePath(file.relativePath)) << ",\n";
        out << "      \"role\": " << Quote(ProjectFileRoleToString(file.role)) << ",\n";
        out << "      \"state\": " << Quote(ProjectFileStateToString(file.state)) << ",\n";
        out << "      \"hashAlgorithm\": " << Quote(file.hashAlgorithm) << ",\n";
        out << "      \"rawHash\": " << Quote(file.rawHash) << ",\n";
        out << "      \"semanticHash\": " << Quote(file.semanticHash) << ",\n";
        out << "      \"elementIndexHash\": " << Quote(file.elementIndexHash) << ",\n";
        out << "      \"relationshipGraphHash\": " << Quote(file.relationshipGraphHash) << ",\n";
        out << "      \"sacm\": {\n";
        out << "        \"parseStatus\": " << Quote(file.parseStatus) << "\n";
        out << "      }";
        if (!file.lastError.empty()) {
            out << ",\n      \"lastError\": " << Quote(file.lastError) << "\n";
        } else {
            out << "\n";
        }
        out << "    }" << (i + 1 == project.files.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"dependencies\": [],\n";
    out << "  \"baselines\": [],\n";
    out << "  \"settings\": {\n";
    out << "    \"defaultLanguage\": " << Quote(project.defaultLanguage) << ",\n";
    out << "    \"validationMode\": " << Quote(project.validationMode) << "\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

bool ParseManifest(const std::string& text,
                   const std::filesystem::path& root_path,
                   AssuranceProject& project,
                   std::string& error) {
    if (JsonStringValue(text, "format") != kProjectFormat) {
        error = "af.proj has an unsupported format";
        return false;
    }

    project = AssuranceProject{};
    project.rootPath = root_path;
    project.formatVersion = JsonStringValue(text, "formatVersion", kProjectFormatVersion);
    if (project.formatVersion != kProjectFormatVersion) {
        error = "af.proj version is unsupported: " + project.formatVersion;
        return false;
    }

    std::string project_section = JsonObjectSection(text, "project");
    std::string tool_section = JsonObjectSection(text, "tool");
    std::string settings_section = JsonObjectSection(text, "settings");

    project.id = JsonStringValue(project_section, "id");
    project.name = JsonStringValue(project_section, "name");
    project.description = JsonStringValue(project_section, "description");
    project.createdUtc = JsonStringValue(project_section, "createdUtc");
    project.modifiedUtc = JsonStringValue(project_section, "modifiedUtc");
    project.createdWith = JsonStringValue(tool_section, "createdWith", "Assurance Forge");
    project.lastOpenedWith = JsonStringValue(tool_section, "lastOpenedWith", "Assurance Forge");
    project.defaultLanguage = JsonStringValue(settings_section, "defaultLanguage", "en");
    project.validationMode = JsonStringValue(settings_section, "validationMode", "permissive");

    if (project.id.empty() || project.name.empty()) {
        error = "af.proj is missing project identity fields";
        return false;
    }

    for (const auto& file_object : TopLevelObjectsInArray(JsonArraySection(text, "files"))) {
        ProjectFileEntry entry;
        entry.id = JsonStringValue(file_object, "id");
        entry.relativePath = std::filesystem::path(JsonStringValue(file_object, "path"));
        entry.role = ProjectFileRoleFromString(JsonStringValue(file_object, "role"));
        entry.hashAlgorithm = JsonStringValue(file_object, "hashAlgorithm", "sha256");
        entry.rawHash = JsonStringValue(file_object, "rawHash");
        entry.semanticHash = JsonStringValue(file_object, "semanticHash");
        entry.elementIndexHash = JsonStringValue(file_object, "elementIndexHash");
        entry.relationshipGraphHash = JsonStringValue(file_object, "relationshipGraphHash");
        entry.parseStatus = JsonStringValue(JsonObjectSection(file_object, "sacm"), "parseStatus", "notParsed");
        entry.lastError = JsonStringValue(file_object, "lastError");
        if (entry.id.empty() || entry.relativePath.empty()) {
            error = "af.proj contains an invalid file entry";
            return false;
        }
        project.files.push_back(std::move(entry));
    }

    return true;
}

bool IsSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    return true;
}

std::filesystem::path NormalizeFileName(const std::string& requested_file_name,
                                        const char* default_name,
                                        const char* extension) {
    std::string trimmed = Trim(requested_file_name);
    if (trimmed.empty()) trimmed = default_name;
    std::filesystem::path name(trimmed);
    std::string actual_extension = name.extension().string();
    std::string expected_extension = extension;
    std::transform(actual_extension.begin(), actual_extension.end(), actual_extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(expected_extension.begin(), expected_extension.end(), expected_extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (actual_extension != expected_extension) {
        name += extension;
    }
    return name.filename();
}

std::string MinimalSacmXml(const std::string& case_name) {
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
           "<sacm:AssuranceCasePackage xmlns:sacm=\"http://www.omg.org/spec/SACM/2.2/Argumentation\" "
           "id=\"ACP_EMPTY\" name=\"" + EscapeXmlAttribute(case_name) + "\">\n"
           "  <argumentPackage id=\"AP_MAIN\" name=\"Main Argument\">\n"
           "    <claim id=\"G1\" name=\"New Goal\" />\n"
           "  </argumentPackage>\n"
           "</sacm:AssuranceCasePackage>\n";
}

std::string EvidenceRegisterJson() {
    return "{\n"
           "  \"format\": \"assurance-forge-evidence-register\",\n"
           "  \"formatVersion\": \"0.1.0\",\n"
           "  \"entries\": []\n"
           "}\n";
}

std::string J3377RegisterJson() {
    return "{\n"
           "  \"format\": \"assurance-forge-j3377-cae-register\",\n"
           "  \"formatVersion\": \"0.1.0\",\n"
           "  \"entries\": []\n"
           "}\n";
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
        semantic_lines.push_back(element.id + "|" + element.type + "|" + element.name + "|" +
                                 element.content + "|" + element.assertion_declaration);
        if (!element.source_refs.empty() || !element.target_refs.empty()) {
            for (const auto& source : element.source_refs) {
                for (const auto& target : element.target_refs) {
                    relationship_lines.push_back(source + "|" + target + "|" + element.type + "|" + element.reasoning_ref);
                }
            }
        }
    }

    auto join_and_hash = [&entry](std::vector<std::string> lines, std::string& hash) -> bool {
        std::sort(lines.begin(), lines.end());
        std::ostringstream normalized;
        for (const auto& line : lines) normalized << line << '\n';
        std::string hash_error;
        if (!Sha256String(normalized.str(), hash, hash_error)) {
            entry.state = ProjectFileState::ParseError;
            entry.lastError = "Hash computation failed: " + hash_error;
            return false;
        }
        return true;
    };

    if (!join_and_hash(semantic_lines, entry.semanticHash)) return;
    if (!join_and_hash(element_ids, entry.elementIndexHash)) return;
    if (!join_and_hash(relationship_lines, entry.relationshipGraphHash)) return;
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
    if (!Sha256File(absolute_path, entry.rawHash, error)) return false;

    if (entry.role == ProjectFileRole::SacmArgument) {
        ComputeSacmHashes(entry, absolute_path);
    } else {
        entry.parseStatus = "notParsed";
        entry.lastError.clear();
    }

    if (entry.state != ProjectFileState::ParseError) {
        if (detect_external_change && !previous_raw_hash.empty() && previous_raw_hash != entry.rawHash) {
            entry.state = entry.role == ProjectFileRole::SacmArgument
                              ? ProjectFileState::ModifiedButCompatible
                              : ProjectFileState::ModifiedOutsideAssuranceForge;
        } else {
            entry.state = ProjectFileState::Clean;
        }
    }
    return true;
}

bool AddTrackedFile(AssuranceProject& project,
                    const std::filesystem::path& folder,
                    const std::filesystem::path& file_name,
                    ProjectFileRole role,
                    const std::string& content,
                    ProjectFileEntry& entry,
                    std::string& error) {
    std::filesystem::path relative_path = folder / file_name;
    if (!IsSafeRelativePath(relative_path)) {
        error = "Invalid project file path";
        return false;
    }

    std::filesystem::path absolute_path = project.rootPath / relative_path;
    std::error_code ec;
    if (std::filesystem::exists(absolute_path, ec)) {
        error = "File already exists: " + relative_path.generic_string();
        return false;
    }

    if (!WriteTextFile(absolute_path, content, error)) return false;

    ProjectFileEntry new_entry;
    new_entry.id = GenerateId("af-file");
    new_entry.relativePath = relative_path;
    new_entry.role = role;
    new_entry.hashAlgorithm = "sha256";
    if (!RefreshEntryHashes(project, new_entry, false, error)) {
        std::filesystem::remove(absolute_path, ec);
        return false;
    }

    project.files.push_back(new_entry);
    project.modifiedUtc = NowUtc();

    if (!ProjectService::WriteManifestSafely(project, error)) {
        project.files.pop_back();
        std::filesystem::remove(absolute_path, ec);
        return false;
    }

    entry = new_entry;
    return true;
}

void AddStep(ProjectLoadReport& report,
             const std::string& label,
             ProjectLoadStepStatus status,
             const std::string& message = {}) {
    report.steps.push_back(ProjectLoadStep{label, status, message});
}

}  // namespace

std::filesystem::path ProjectService::ManifestPath(const AssuranceProject& project) {
    return project.rootPath / kManifestFileName;
}

bool ProjectService::CreateEmptyProject(const std::string& project_name,
                                        const std::filesystem::path& parent_location,
                                        AssuranceProject& project,
                                        ProjectLoadReport& report,
                                        std::string& error) {
    std::string clean_name = Trim(project_name);
    if (clean_name.empty()) {
        error = "Project name is required.";
        return false;
    }
    if (parent_location.empty()) {
        error = "Project location is required.";
        return false;
    }

    std::filesystem::path root = parent_location / clean_name;
    std::error_code ec;
    if (std::filesystem::exists(root, ec)) {
        error = "Project folder already exists: " + root.string();
        return false;
    }

    for (const char* directory : kProjectDirectories) {
        if (!std::filesystem::create_directories(root / directory, ec) && ec) {
            error = "Could not create project directory: " + (root / directory).string();
            return false;
        }
    }

    project = AssuranceProject{};
    project.id = GenerateId("af-project");
    project.name = clean_name;
    project.rootPath = root;
    project.createdUtc = NowUtc();
    project.modifiedUtc = project.createdUtc;

    if (!WriteManifestSafely(project, error)) return false;

    ProjectFileEntry main_entry;
    if (!AddSacmFile(project, "main.sacm", main_entry, error)) return false;

    report = RefreshFileStatus(project);
    report.showPopup = true;
    return true;
}

bool ProjectService::OpenProject(const std::filesystem::path& project_or_manifest_path,
                                 AssuranceProject& project,
                                 ProjectLoadReport& report,
                                 std::string& error) {
    std::filesystem::path manifest = project_or_manifest_path;
    std::error_code ec;
    if (std::filesystem::is_directory(manifest, ec)) {
        manifest /= kManifestFileName;
    }

    report = ProjectLoadReport{};
    std::string manifest_text = ReadTextFile(manifest, error);
    if (!error.empty()) {
        AddStep(report, "Load af.proj", ProjectLoadStepStatus::Failed, error);
        report.showPopup = true;
        return false;
    }

    AssuranceProject loaded;
    if (!ParseManifest(manifest_text, manifest.parent_path(), loaded, error)) {
        AddStep(report, "Load af.proj", ProjectLoadStepStatus::Failed, error);
        report.showPopup = true;
        return false;
    }
    AddStep(report, "Load af.proj", ProjectLoadStepStatus::Passed, manifest.string());

    loaded.lastOpenedWith = "Assurance Forge";
    report = RefreshFileStatus(loaded);
    report.steps.insert(report.steps.begin(), ProjectLoadStep{"Load af.proj", ProjectLoadStepStatus::Passed, manifest.string()});
    report.showPopup = true;

    project = std::move(loaded);
    return true;
}

bool ProjectService::AddSacmFile(AssuranceProject& project,
                                 const std::string& requested_file_name,
                                 ProjectFileEntry& entry,
                                 std::string& error) {
    std::filesystem::path file_name = NormalizeFileName(requested_file_name, "main.sacm", ".sacm");
    return AddTrackedFile(project, "arguments", file_name, ProjectFileRole::SacmArgument,
                          MinimalSacmXml(project.name), entry, error);
}

bool ProjectService::AddEvidenceRegister(AssuranceProject& project,
                                         const std::string& requested_file_name,
                                         ProjectFileEntry& entry,
                                         std::string& error) {
    std::filesystem::path file_name = NormalizeFileName(requested_file_name, "evidence-register.af.json", ".json");
    return AddTrackedFile(project, "registers", file_name, ProjectFileRole::EvidenceRegister,
                          EvidenceRegisterJson(), entry, error);
}

bool ProjectService::AddJ3377CaeRegister(AssuranceProject& project,
                                         const std::string& requested_file_name,
                                         ProjectFileEntry& entry,
                                         std::string& error) {
    std::filesystem::path file_name = NormalizeFileName(requested_file_name, "j3377-cae-register.af.json", ".json");
    return AddTrackedFile(project, "registers", file_name, ProjectFileRole::J3377CaeRegister,
                          J3377RegisterJson(), entry, error);
}

bool ProjectService::WriteManifestSafely(const AssuranceProject& project, std::string& error) {
    std::filesystem::path manifest = ManifestPath(project);
    std::filesystem::path temp_manifest = manifest;
    temp_manifest += ".tmp";

    std::string content = SerializeManifest(project);
    AssuranceProject validation_project;
    if (!ParseManifest(content, project.rootPath, validation_project, error)) {
        error = "Generated af.proj did not validate: " + error;
        return false;
    }

    if (!WriteTextFile(temp_manifest, content, error)) return false;

    std::error_code ec;
    if (std::filesystem::exists(manifest, ec)) {
        std::filesystem::path backup_dir = project.rootPath / ".af" / "backups" / BackupTimestampUtc();
        if (!std::filesystem::create_directories(backup_dir, ec) && ec) {
            error = "Could not create backup directory: " + backup_dir.string();
            std::filesystem::remove(temp_manifest, ec);
            return false;
        }
        std::filesystem::copy_file(manifest, backup_dir / kManifestFileName,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Could not back up af.proj: " + ec.message();
            std::filesystem::remove(temp_manifest, ec);
            return false;
        }
        std::filesystem::remove(manifest, ec);
        if (ec) {
            error = "Could not replace af.proj: " + ec.message();
            std::filesystem::remove(temp_manifest, ec);
            return false;
        }
    }

    std::filesystem::rename(temp_manifest, manifest, ec);
    if (ec) {
        error = "Could not install af.proj: " + ec.message();
        std::filesystem::remove(temp_manifest, ec);
        return false;
    }
    return true;
}

ProjectLoadReport ProjectService::RefreshFileStatus(AssuranceProject& project) {
    ProjectLoadReport report;
    AddStep(report, "Scan tracked files", ProjectLoadStepStatus::Passed,
            std::to_string(project.files.size()) + " tracked file(s)");

    bool raw_hashes_ok = true;
    bool parsing_ok = true;
    bool semantic_hashes_ok = true;
    size_t changed_count = 0;
    size_t missing_count = 0;

    for (auto& entry : project.files) {
        std::string previous_raw_hash = entry.rawHash;
        std::string error;
        if (!RefreshEntryHashes(project, entry, true, error)) {
            raw_hashes_ok = false;
            entry.lastError = error;
        }
        if (entry.state == ProjectFileState::Missing) {
            ++missing_count;
            raw_hashes_ok = false;
        }
        if (entry.state == ProjectFileState::ModifiedOutsideAssuranceForge ||
            entry.state == ProjectFileState::ModifiedButCompatible) {
            ++changed_count;
            report.warnings.push_back(entry.relativePath.generic_string() + " was modified outside Assurance Forge.");
        }
        if (entry.state == ProjectFileState::ParseError) parsing_ok = false;
        if (entry.role == ProjectFileRole::SacmArgument &&
            (entry.semanticHash.empty() || entry.elementIndexHash.empty() || entry.relationshipGraphHash.empty()) &&
            entry.state != ProjectFileState::ParseError && entry.state != ProjectFileState::Missing) {
            semantic_hashes_ok = false;
        }
        (void)previous_raw_hash;
    }

    AddStep(report,
            "Recalculate raw hashes",
            raw_hashes_ok ? (changed_count > 0 ? ProjectLoadStepStatus::Warning : ProjectLoadStepStatus::Passed)
                          : ProjectLoadStepStatus::Failed,
            changed_count > 0 ? std::to_string(changed_count) + " externally modified file(s)" :
            missing_count > 0 ? std::to_string(missing_count) + " missing file(s)" : "Raw hashes recalculated");
    AddStep(report,
            "Parse changed files",
            parsing_ok ? ProjectLoadStepStatus::Passed : ProjectLoadStepStatus::Failed,
            parsing_ok ? "Changed SACM files parsed" : "One or more SACM files failed to parse");
    AddStep(report,
            "Recalculate semantic hashes",
            semantic_hashes_ok ? ProjectLoadStepStatus::Passed : ProjectLoadStepStatus::Failed,
            semantic_hashes_ok ? "Semantic hashes recalculated" : "Could not calculate one or more semantic hashes");
    AddStep(report, "Check project references", ProjectLoadStepStatus::Passed,
            "Detailed dependency repair will be implemented later");
    AddStep(report,
            "Report project health",
            report.has_failures() ? ProjectLoadStepStatus::Failed :
            (!report.warnings.empty() ? ProjectLoadStepStatus::Warning : ProjectLoadStepStatus::Passed),
            report.has_failures() ? "Project needs attention" :
            (!report.warnings.empty() ? "Project loaded with warnings" : "Project is healthy"));
    report.showPopup = true;
    return report;
}

}  // namespace core