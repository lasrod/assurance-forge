#include "core/translation_review_store.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace core::translation {
namespace {

constexpr const char* kFormat = "assurance-forge-translation-review";
constexpr const char* kFormatVersion = "0.1.0";

} // namespace

std::string SerializeTranslationReview(const std::vector<std::string>& element_ids) {
    nlohmann::json root;
    root["format"] = kFormat;
    root["formatVersion"] = kFormatVersion;
    nlohmann::json items = nlohmann::json::array();
    for (const std::string& element_id : element_ids) {
        if (element_id.empty())
            continue;
        items.push_back(element_id);
    }
    root["pending"] = std::move(items);
    return root.dump(2) + "\n";
}

bool ParseTranslationReview(const std::string& json,
                            std::vector<std::string>& out_element_ids,
                            std::string& error) {
    out_element_ids.clear();
    error.clear();

    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded()) {
        error = "Translation review file is not valid JSON.";
        return false;
    }
    if (!root.is_object()) {
        error = "Translation review file has an unexpected shape.";
        return false;
    }

    const auto pending = root.find("pending");
    if (pending == root.end())
        return true; // No entries recorded yet.
    if (!pending->is_array()) {
        error = "Translation review 'pending' field must be an array.";
        return false;
    }

    for (const nlohmann::json& entry : *pending) {
        if (!entry.is_string())
            continue;
        std::string element_id = entry.get<std::string>();
        if (element_id.empty())
            continue;
        out_element_ids.push_back(std::move(element_id));
    }
    return true;
}

} // namespace core::translation
