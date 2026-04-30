#pragma once

#include <optional>
#include <string>
#include <vector>

namespace core {

enum class ReviewItemStatus {
    Open,
    Resolved,
};

enum class ReviewItemSource {
    Manual,
    AIReview,
};

struct ReviewItem {
    std::string id;
    std::string element_id;
    std::string title;
    std::string message;
    std::string severity;
    ReviewItemSource source = ReviewItemSource::Manual;
    ReviewItemStatus status = ReviewItemStatus::Open;
    std::optional<std::string> proposal_id;
    std::string applied_note;
    std::string created_utc;
    std::string updated_utc;
};

const char* ReviewItemStatusToString(ReviewItemStatus status);
ReviewItemStatus ReviewItemStatusFromString(const std::string& value);

const char* ReviewItemSourceToString(ReviewItemSource source);
ReviewItemSource ReviewItemSourceFromString(const std::string& value);

std::string SerializeReviewItems(const std::vector<ReviewItem>& items);
bool DeserializeReviewItems(const std::string& content, std::vector<ReviewItem>& items, std::string& error);

}  // namespace core