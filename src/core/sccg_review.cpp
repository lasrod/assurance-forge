#include "core/sccg_review.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace core {
namespace {

std::vector<SccgGuideline> g_guidelines;
std::unordered_map<std::string, std::vector<ElementSccgTag>> g_tags_by_element;
bool g_review_dirty = false;

std::vector<std::string> SplitPipeSeparated(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::stringstream ss(line);
    while (std::getline(ss, field, '|')) {
        out.push_back(field);
    }
    return out;
}

std::string SidecarPathForModel(const std::string& model_path) {
    std::filesystem::path p(model_path);
    p.replace_extension(".sccg-review.jsonl");
    return p.string();
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string JsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            switch (n) {
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += n; break;
            }
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

std::string ExtractJsonStringField(const std::string& line, const char* key) {
    std::string needle = "\"";
    needle += key;
    needle += "\":\"";

    size_t pos = line.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();

    std::string value;
    bool escaping = false;
    for (size_t i = pos; i < line.size(); ++i) {
        char c = line[i];
        if (escaping) {
            value += '\\';
            value += c;
            escaping = false;
            continue;
        }
        if (c == '\\') {
            escaping = true;
            continue;
        }
        if (c == '"') {
            break;
        }
        value += c;
    }

    return JsonUnescape(value);
}

bool IsOpenStatus(const std::string& status) {
    return status.empty() || status == "Open";
}

}  // namespace

bool LoadSccgCatalog(const std::string& catalog_path) {
    g_guidelines.clear();

    std::ifstream in(catalog_path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    bool first_line = true;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // Skip header row.
        if (first_line) {
            first_line = false;
            if (line.find("id|") == 0) {
                continue;
            }
        }

        std::vector<std::string> fields = SplitPipeSeparated(line);
        if (fields.size() < 4) {
            continue;
        }

        SccgGuideline g;
        g.id = fields[0];
        g.category = fields[1];
        g.title = fields[2];
        g.guideline = fields[3];
        g_guidelines.push_back(std::move(g));
    }

    std::sort(g_guidelines.begin(), g_guidelines.end(), [](const SccgGuideline& a, const SccgGuideline& b) {
        return a.id < b.id;
    });

    return !g_guidelines.empty();
}

const std::vector<SccgGuideline>& GetSccgGuidelines() {
    return g_guidelines;
}

const SccgGuideline* FindSccgGuidelineById(const std::string& guideline_id) {
    for (const auto& guideline : g_guidelines) {
        if (guideline.id == guideline_id) {
            return &guideline;
        }
    }
    return nullptr;
}

void ClearReviewData() {
    g_tags_by_element.clear();
    g_review_dirty = false;
}

bool LoadReviewSidecarForModel(const std::string& model_path) {
    g_tags_by_element.clear();
    g_review_dirty = false;

    std::ifstream in(SidecarPathForModel(model_path));
    if (!in.is_open()) {
        // No sidecar yet is a normal state.
        return true;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::string element_id = ExtractJsonStringField(line, "element_id");
        if (element_id.empty()) continue;

        ElementSccgTag tag;
        tag.guideline_id = ExtractJsonStringField(line, "guideline_id");
        tag.status = ExtractJsonStringField(line, "status");
        tag.severity = ExtractJsonStringField(line, "severity");
        tag.comment = ExtractJsonStringField(line, "comment");

        if (tag.status.empty()) tag.status = "Open";
        if (tag.severity.empty()) tag.severity = "Minor";

        g_tags_by_element[element_id].push_back(std::move(tag));
    }

    return true;
}

bool SaveReviewSidecarForModel(const std::string& model_path) {
    std::ofstream out(SidecarPathForModel(model_path), std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    for (const auto& kv : g_tags_by_element) {
        const std::string& element_id = kv.first;
        const std::vector<ElementSccgTag>& tags = kv.second;

        for (const auto& tag : tags) {
            out << "{"
                << "\"element_id\":\"" << JsonEscape(element_id) << "\"," 
                << "\"guideline_id\":\"" << JsonEscape(tag.guideline_id) << "\"," 
                << "\"status\":\"" << JsonEscape(tag.status) << "\"," 
                << "\"severity\":\"" << JsonEscape(tag.severity) << "\"," 
                << "\"comment\":\"" << JsonEscape(tag.comment) << "\""
                << "}" << '\n';
        }
    }

    g_review_dirty = false;
    return true;
}

std::vector<ElementSccgTag> GetTagsForElement(const std::string& element_id) {
    auto it = g_tags_by_element.find(element_id);
    if (it == g_tags_by_element.end()) {
        return {};
    }
    return it->second;
}

void SetTagsForElement(const std::string& element_id, const std::vector<ElementSccgTag>& tags) {
    if (tags.empty()) {
        g_tags_by_element.erase(element_id);
        g_review_dirty = true;
        return;
    }

    g_tags_by_element[element_id] = tags;
    g_review_dirty = true;
}

bool HasPendingReviewChanges() {
    return g_review_dirty;
}

int CountTaggedElements() {
    int count = 0;
    for (const auto& kv : g_tags_by_element) {
        if (!kv.second.empty()) {
            ++count;
        }
    }
    return count;
}

int CountOpenFindings() {
    int count = 0;
    for (const auto& kv : g_tags_by_element) {
        for (const auto& tag : kv.second) {
            if (IsOpenStatus(tag.status)) {
                ++count;
            }
        }
    }
    return count;
}

}  // namespace core
