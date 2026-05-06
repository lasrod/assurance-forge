#pragma once

#include "core/reviews/review_item.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace core::reviews {

class ReviewItemManager {
public:
    void SetFilePath(std::filesystem::path file_path);
    const std::filesystem::path& FilePath() const {
        return file_path_;
    }

    bool Load(std::string& error);
    bool Save(std::string& error) const;
    void Clear();

    const std::vector<ReviewItem>& GetItems() const {
        return items_;
    }
    std::vector<ReviewItem> GetItemsForElement(const std::string& element_id) const;
    std::optional<ReviewItem> GetItemById(const std::string& id) const;
    const ElementReviewStateMap& GetElementReviewStates() const {
        return element_states_;
    }
    ElementReviewState GetElementReviewState(const std::string& element_id) const;

    bool AddOrUpdateItem(ReviewItem item);
    bool RemoveItem(const std::string& id);
    size_t RemoveItemsForElementSourceAndIdPrefix(const std::string& element_id,
                                                  ReviewItemSource source,
                                                  const std::string& id_prefix);
    bool SetProposal(const std::string& review_item_id, const std::string& proposal_id);
    bool ClearProposal(const std::string& review_item_id);
    bool SetElementReviewState(const std::string& element_id, ElementReviewState state);
    bool ClearElementReviewState(const std::string& element_id);

private:
    std::filesystem::path file_path_;
    std::vector<ReviewItem> items_;
    ElementReviewStateMap element_states_;
};

} // namespace core::reviews
