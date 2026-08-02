#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace core::reviews {

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
    std::string reviewer_name;
    std::vector<std::string> guideline_ids;
    // Unified draft groups that implement suggested corrections for this
    // finding. `proposal_id` remains readable during legacy migration only.
    std::vector<std::string> draft_group_ids;
    ReviewItemSource source = ReviewItemSource::Manual;
    ReviewItemStatus status = ReviewItemStatus::Open;
    std::optional<std::string> proposal_id;
    std::string applied_note;
    std::string created_utc;
    std::string updated_utc;
};

struct ElementReviewState {
    bool manual_ok = false;
    bool ai_ok = false;
    bool failed = false;
    std::string review_profile_id;
    std::string review_profile_name;
    std::string last_review_message;
    std::string reviewed_by;
    std::string updated_utc;
};

using ElementReviewStateMap = std::unordered_map<std::string, ElementReviewState>;

const char* ReviewItemStatusToString(ReviewItemStatus status);
ReviewItemStatus ReviewItemStatusFromString(const std::string& value);

const char* ReviewItemSourceToString(ReviewItemSource source);
ReviewItemSource ReviewItemSourceFromString(const std::string& value);

std::string SerializeReviewItems(const std::vector<ReviewItem>& items);
std::string SerializeReviewItems(const std::vector<ReviewItem>& items, const ElementReviewStateMap& element_states);
bool DeserializeReviewItems(const std::string& content, std::vector<ReviewItem>& items, std::string& error);
bool DeserializeReviewItems(const std::string& content,
                            std::vector<ReviewItem>& items,
                            ElementReviewStateMap& element_states,
                            std::string& error);

} // namespace core::reviews
