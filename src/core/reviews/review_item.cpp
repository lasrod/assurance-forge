#include "core/reviews/review_item.h"

#include <nlohmann/json.hpp>

namespace core {

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
    object["source"] = ReviewItemSourceToString(item.source);
    object["status"] = ReviewItemStatusToString(item.status);
    if (item.proposal_id.has_value()) object["proposal_id"] = item.proposal_id.value();
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

}  // namespace

const char* ReviewItemStatusToString(ReviewItemStatus status) {
    switch (status) {
        case ReviewItemStatus::Open: return "open";
        case ReviewItemStatus::Resolved: return "resolved";
    }
    return "open";
}

ReviewItemStatus ReviewItemStatusFromString(const std::string& value) {
    if (value == "resolved") return ReviewItemStatus::Resolved;
    return ReviewItemStatus::Open;
}

const char* ReviewItemSourceToString(ReviewItemSource source) {
    switch (source) {
        case ReviewItemSource::Manual: return "manual";
        case ReviewItemSource::AIReview: return "aiReview";
    }
    return "manual";
}

ReviewItemSource ReviewItemSourceFromString(const std::string& value) {
    if (value == "aiReview") return ReviewItemSource::AIReview;
    return ReviewItemSource::Manual;
}

std::string SerializeReviewItems(const std::vector<ReviewItem>& items) {
    nlohmann::json root;
    root["format"] = kReviewItemsFormat;
    root["formatVersion"] = kReviewItemsFormatVersion;
    root["items"] = nlohmann::json::array();
    for (const ReviewItem& item : items) {
        root["items"].push_back(ToJson(item));
    }
    return root.dump(2) + "\n";
}

bool DeserializeReviewItems(const std::string& content, std::vector<ReviewItem>& items, std::string& error) {
    items.clear();
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
            if (!entry.is_object()) continue;
            ReviewItem item = FromJson(entry);
            if (!item.id.empty()) items.push_back(std::move(item));
        }
    } catch (const nlohmann::json::exception& e) {
        error = std::string("Review item JSON parse failed: ") + e.what();
        return false;
    }
    return true;
}

}  // namespace core