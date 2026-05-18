#include "core/project_manifest.h"

#include <iomanip>
#include <sstream>
#include <vector>

namespace core {
namespace {

constexpr const char* kProjectFormat = "assurance-forge-project";
constexpr const char* kProjectFormatVersion = "0.1.0";

std::string EscapeJson(const std::string& value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
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

std::string ToGenericRelativePath(const std::filesystem::path& path) {
    return path.generic_string();
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
            if (depth == 0)
                return i;
        }
    }
    return std::string::npos;
}

size_t FindJsonKey(const std::string& object, const std::string& key) {
    return object.find(Quote(key));
}

bool ParseJsonStringAt(const std::string& text, size_t quote_pos, std::string& value) {
    if (quote_pos == std::string::npos || quote_pos >= text.size() || text[quote_pos] != '"')
        return false;
    value.clear();
    bool escape = false;
    for (size_t i = quote_pos + 1; i < text.size(); ++i) {
        char c = text[i];
        if (escape) {
            switch (c) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '/':
                value.push_back('/');
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                value.push_back(c);
                break;
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
    if (key_pos == std::string::npos)
        return fallback;
    size_t colon = object.find(':', key_pos);
    if (colon == std::string::npos)
        return fallback;
    size_t quote = object.find('"', colon + 1);
    std::string value;
    if (!ParseJsonStringAt(object, quote, value))
        return fallback;
    return value;
}

std::string JsonObjectSection(const std::string& object, const std::string& key) {
    size_t key_pos = FindJsonKey(object, key);
    if (key_pos == std::string::npos)
        return {};
    size_t open = object.find('{', key_pos);
    if (open == std::string::npos)
        return {};
    size_t close = FindMatching(object, open, '{', '}');
    if (close == std::string::npos)
        return {};
    return object.substr(open, close - open + 1);
}

std::string JsonArraySection(const std::string& object, const std::string& key) {
    size_t key_pos = FindJsonKey(object, key);
    if (key_pos == std::string::npos)
        return {};
    size_t open = object.find('[', key_pos);
    if (open == std::string::npos)
        return {};
    size_t close = FindMatching(object, open, '[', ']');
    if (close == std::string::npos)
        return {};
    return object.substr(open, close - open + 1);
}

std::vector<std::string> TopLevelObjectsInArray(const std::string& array_text) {
    std::vector<std::string> objects;
    size_t pos = 0;
    while (true) {
        size_t open = array_text.find('{', pos);
        if (open == std::string::npos)
            break;
        size_t close = FindMatching(array_text, open, '{', '}');
        if (close == std::string::npos)
            break;
        objects.push_back(array_text.substr(open, close - open + 1));
        pos = close + 1;
    }
    return objects;
}

} // namespace

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
    if (!project.files.empty())
        out << "\n";
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

} // namespace core
