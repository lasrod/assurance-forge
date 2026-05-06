#include "core/reviews/review_item.h"

#include <nlohmann/json.hpp>

namespace core::reviews {

namespace {

constexpr const char* kReviewItemsFormat = "assurance-forge-review-items";
constexpr const char* kReviewItemsFormatVersion = "0.1.0";

nlohmann::json ToJson(const ReviewItem& item) {
    nlohmann::json object;
    object["id"] = item.id;
    object["element_id"] = item.element_id;
    object["title"] = item.title;
    object["message"] = item.message;
    object["severity"] = item.severity;
    object["reviewer_name"] = item.reviewer_name;
    object["guideline_ids"] = item.guideline_ids;
    object["source"] = ReviewItemSourceToString(item.source);
    object["status"] = ReviewItemStatusToString(item.status);
    if (item.proposal_id.has_value())
        object["proposal_id"] = item.proposal_id.value();
    object["applied_note"] = item.applied_note;
    object["created_utc"] = item.created_utc;
    object["updated_utc"] = item.updated_utc;
    return object;
}

ReviewItem FromJson(const nlohmann::json& object) {
    ReviewItem item;
    item.id = object.value("id", "");
    item.element_id = object.value("element_id", "");
    item.title = object.value("title", "");
    item.message = object.value("message", "");
    item.severity = object.value("severity", "");
    item.reviewer_name = object.value("reviewer_name", "");
    if (object.contains("guideline_ids") && object["guideline_ids"].is_array()) {
        for (const auto& guideline_id : object["guideline_ids"]) {
            if (guideline_id.is_string())
                item.guideline_ids.push_back(guideline_id.get<std::string>());
        }
    }
    item.source = ReviewItemSourceFromString(object.value("source", "manual"));
    item.status = ReviewItemStatusFromString(object.value("status", "open"));
    if (object.contains("proposal_id") && object["proposal_id"].is_string()) {
        item.proposal_id = object["proposal_id"].get<std::string>();
    }
    item.applied_note = object.value("applied_note", "");
    item.created_utc = object.value("created_utc", "");
    item.updated_utc = object.value("updated_utc", "");
    return item;
}

nlohmann::json ToJson(const ElementReviewState& state) {
    nlohmann::json object;
    object["manual_ok"] = state.manual_ok;
    object["ai_ok"] = state.ai_ok;
    object["failed"] = state.failed;
    if (!state.review_profile_id.empty())
        object["review_profile_id"] = state.review_profile_id;
    if (!state.review_profile_name.empty())
        object["review_profile_name"] = state.review_profile_name;
    if (!state.last_review_message.empty())
        object["last_review_message"] = state.last_review_message;
    if (!state.reviewed_by.empty())
        object["reviewed_by"] = state.reviewed_by;
    if (!state.updated_utc.empty())
        object["updated_utc"] = state.updated_utc;
    return object;
}

ElementReviewState ElementReviewStateFromJson(const nlohmann::json& object) {
    ElementReviewState state;
    state.manual_ok = object.value("manual_ok", false);
    state.ai_ok = object.value("ai_ok", false);
    state.failed = object.value("failed", false);
    state.review_profile_id = object.value("review_profile_id", "");
    state.review_profile_name = object.value("review_profile_name", "");
    state.last_review_message = object.value("last_review_message", "");
    state.reviewed_by = object.value("reviewed_by", "");
    state.updated_utc = object.value("updated_utc", "");
    return state;
}

bool IsEmpty(const ElementReviewState& state) {
    return !state.manual_ok && !state.ai_ok && !state.failed && state.review_profile_id.empty() &&
           state.review_profile_name.empty() && state.last_review_message.empty() && state.reviewed_by.empty() &&
           state.updated_utc.empty();
}

} // namespace

const char* ReviewItemStatusToString(ReviewItemStatus status) {
    switch (status) {
    case ReviewItemStatus::Open:
        return "open";
    case ReviewItemStatus::Resolved:
        return "resolved";
    }
    return "open";
}

ReviewItemStatus ReviewItemStatusFromString(const std::string& value) {
    if (value == "resolved")
        return ReviewItemStatus::Resolved;
    return ReviewItemStatus::Open;
}

const char* ReviewItemSourceToString(ReviewItemSource source) {
    switch (source) {
    case ReviewItemSource::Manual:
        return "manual";
    case ReviewItemSource::AIReview:
        return "aiReview";
    }
    return "manual";
}

ReviewItemSource ReviewItemSourceFromString(const std::string& value) {
    if (value == "aiReview")
        return ReviewItemSource::AIReview;
    return ReviewItemSource::Manual;
}

std::string SerializeReviewItems(const std::vector<ReviewItem>& items) {
    return SerializeReviewItems(items, {});
}

std::string SerializeReviewItems(const std::vector<ReviewItem>& items, const ElementReviewStateMap& element_states) {
    nlohmann::json root;
    root["format"] = kReviewItemsFormat;
    root["formatVersion"] = kReviewItemsFormatVersion;
    root["items"] = nlohmann::json::array();
    for (const ReviewItem& item : items) {
        root["items"].push_back(ToJson(item));
    }
    if (!element_states.empty()) {
        root["element_review_states"] = nlohmann::json::object();
        for (const auto& [element_id, state] : element_states) {
            if (!element_id.empty() && !IsEmpty(state))
                root["element_review_states"][element_id] = ToJson(state);
        }
    }
    return root.dump(2) + "\n";
}

bool DeserializeReviewItems(const std::string& content, std::vector<ReviewItem>& items, std::string& error) {
    ElementReviewStateMap ignored_states;
    return DeserializeReviewItems(content, items, ignored_states, error);
}

bool DeserializeReviewItems(const std::string& content,
                            std::vector<ReviewItem>& items,
                            ElementReviewStateMap& element_states,
                            std::string& error) {
    items.clear();
    element_states.clear();
    error.clear();
    try {
        nlohmann::json root = nlohmann::json::parse(content);
        if (!root.is_object() || root.value("format", "") != kReviewItemsFormat) {
            error = "Review item file has an unsupported format.";
            return false;
        }
        if (root.value("formatVersion", "") != kReviewItemsFormatVersion) {
            error = "Review item file has an unsupported version.";
            return false;
        }
        const nlohmann::json entries = root.value("items", nlohmann::json::array());
        if (!entries.is_array()) {
            error = "Review item file items field is not an array.";
            return false;
        }
        for (const auto& entry : entries) {
            if (!entry.is_object())
                continue;
            ReviewItem item = FromJson(entry);
            if (!item.id.empty())
                items.push_back(std::move(item));
        }
        if (root.contains("element_review_states")) {
            if (!root["element_review_states"].is_object()) {
                error = "Review item file element_review_states field is not an object.";
                return false;
            }
            for (const auto& state_entry : root["element_review_states"].items()) {
                if (!state_entry.value().is_object())
                    continue;
                ElementReviewState state = ElementReviewStateFromJson(state_entry.value());
                if (!state_entry.key().empty() && !IsEmpty(state))
                    element_states[state_entry.key()] = std::move(state);
            }
        }
    } catch (const nlohmann::json::exception& e) {
        error = std::string("Review item JSON parse failed: ") + e.what();
        return false;
    }
    return true;
}

} // namespace core::reviews