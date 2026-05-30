#include "core/ignored_terminology_store.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace core::terminology {
namespace {

constexpr const char* kFormat = "assurance-forge-ignored-terminology";
constexpr const char* kFormatVersion = "0.1.0";

} // namespace

std::string SerializeIgnoredSuggestions(const std::vector<IgnoredSuggestion>& suggestions) {
    nlohmann::json root;
    root["format"] = kFormat;
    root["formatVersion"] = kFormatVersion;
    nlohmann::json items = nlohmann::json::array();
    for (const IgnoredSuggestion& suggestion : suggestions) {
        nlohmann::json entry;
        entry["element_id"] = suggestion.element_id;
        entry["term"] = suggestion.term;
        items.push_back(std::move(entry));
    }
    root["ignored"] = std::move(items);
    return root.dump(2) + "\n";
}

bool ParseIgnoredSuggestions(const std::string& json,
                             std::vector<IgnoredSuggestion>& out_suggestions,
                             std::string& error) {
    out_suggestions.clear();
    error.clear();

    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded()) {
        error = "Ignored terminology file is not valid JSON.";
        return false;
    }
    if (!root.is_object()) {
        error = "Ignored terminology file has an unexpected shape.";
        return false;
    }

    const auto ignored = root.find("ignored");
    if (ignored == root.end())
        return true; // No entries recorded yet.
    if (!ignored->is_array()) {
        error = "Ignored terminology 'ignored' field must be an array.";
        return false;
    }

    for (const nlohmann::json& entry : *ignored) {
        if (!entry.is_object())
            continue;
        const auto element_id = entry.find("element_id");
        const auto term = entry.find("term");
        if (element_id == entry.end() || !element_id->is_string() || term == entry.end() || !term->is_string())
            continue;
        IgnoredSuggestion suggestion;
        suggestion.element_id = element_id->get<std::string>();
        suggestion.term = term->get<std::string>();
        if (suggestion.term.empty())
            continue;
        out_suggestions.push_back(std::move(suggestion));
    }
    return true;
}

} // namespace core::terminology
