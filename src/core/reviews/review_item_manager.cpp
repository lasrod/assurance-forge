#include "core/reviews/review_item_manager.h"

#include "core/project_file_io.h"

#include <algorithm>
#include <filesystem>

namespace core::reviews {

void ReviewItemManager::SetFilePath(std::filesystem::path file_path) {
    file_path_ = std::move(file_path);
}

// All or nothing.
//
// This used to clear what it held before it knew whether it could replace it, so
// a failed load emptied the review items as a side effect of failing. The review
// file has a second writer -- another Assurance Forge process, or a text
// editor -- and `ReviewController::ReloadIfChangedExternally` polls it, so a
// read that lands mid-flush parses as garbage. The user's review comments then
// vanished from the panel until the next successful poll, from a read that
// changed nothing on disk. A caller that wants them gone says `Clear`.
bool ReviewItemManager::Load(std::string& error) {
    if (file_path_.empty()) {
        error = "Review item file path is not set.";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(file_path_, ec)) {
        error = "Review item file does not exist: " + file_path_.string();
        return false;
    }

    std::vector<ReviewItem> items;
    ElementReviewStateMap element_states;
    const std::expected<std::string, std::string> text = ReadTextFile(file_path_);
    if (!text) {
        error = text.error();
        return false;
    }
    if (!DeserializeReviewItems(*text, items, element_states, error)) {
        return false;
    }
    items_ = std::move(items);
    element_states_ = std::move(element_states);
    return true;
}

bool ReviewItemManager::Save(std::string& error) const {
    if (file_path_.empty()) {
        error = "Review item file path is not set.";
        return false;
    }
    const std::expected<void, std::string> written =
        WriteTextFileCreatingParents(file_path_, SerializeReviewItems(items_, element_states_));
    if (!written) {
        error = written.error();
        return false;
    }
    return true;
}

void ReviewItemManager::Clear() {
    file_path_.clear();
    items_.clear();
    element_states_.clear();
}

std::vector<ReviewItem> ReviewItemManager::GetItemsForElement(const std::string& element_id) const {
    std::vector<ReviewItem> matches;
    for (const ReviewItem& item : items_) {
        if (item.element_id == element_id)
            matches.push_back(item);
    }
    return matches;
}

std::optional<ReviewItem> ReviewItemManager::GetItemById(const std::string& id) const {
    auto found = std::find_if(items_.begin(), items_.end(), [&](const ReviewItem& item) { return item.id == id; });
    if (found == items_.end())
        return std::nullopt;
    return *found;
}

ElementReviewState ReviewItemManager::GetElementReviewState(const std::string& element_id) const {
    auto found = element_states_.find(element_id);
    if (found == element_states_.end())
        return {};
    return found->second;
}

bool ReviewItemManager::AddOrUpdateItem(ReviewItem item) {
    if (item.id.empty())
        return false;
    auto found =
        std::find_if(items_.begin(), items_.end(), [&](const ReviewItem& existing) { return existing.id == item.id; });
    if (found == items_.end()) {
        items_.push_back(std::move(item));
    } else {
        *found = std::move(item);
    }
    return true;
}

bool ReviewItemManager::RemoveItem(const std::string& id) {
    return std::erase_if(items_, [&](const ReviewItem& item) { return item.id == id; }) > 0;
}

size_t ReviewItemManager::RemoveItemsForElementSourceAndIdPrefix(const std::string& element_id,
                                                                 ReviewItemSource source,
                                                                 const std::string& id_prefix) {
    return std::erase_if(items_, [&](const ReviewItem& item) {
        return item.element_id == element_id && item.source == source && item.id.rfind(id_prefix, 0) == 0;
    });
}

bool ReviewItemManager::SetProposal(const std::string& review_item_id, const std::string& proposal_id) {
    for (ReviewItem& item : items_) {
        if (item.id != review_item_id)
            continue;
        item.proposal_id = proposal_id;
        return true;
    }
    return false;
}

bool ReviewItemManager::ClearProposal(const std::string& review_item_id) {
    for (ReviewItem& item : items_) {
        if (item.id != review_item_id)
            continue;
        item.proposal_id.reset();
        return true;
    }
    return false;
}

bool ReviewItemManager::AddDraftGroup(const std::string& review_item_id, const std::string& group_id) {
    if (group_id.empty())
        return false;
    for (ReviewItem& item : items_) {
        if (item.id != review_item_id)
            continue;
        if (std::find(item.draft_group_ids.begin(), item.draft_group_ids.end(), group_id) ==
            item.draft_group_ids.end()) {
            item.draft_group_ids.push_back(group_id);
        }
        return true;
    }
    return false;
}

bool ReviewItemManager::SetElementReviewState(const std::string& element_id, ElementReviewState state) {
    if (element_id.empty())
        return false;
    element_states_[element_id] = std::move(state);
    return true;
}

bool ReviewItemManager::ClearElementReviewState(const std::string& element_id) {
    return element_states_.erase(element_id) > 0;
}

} // namespace core::reviews
