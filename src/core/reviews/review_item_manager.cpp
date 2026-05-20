#include "core/reviews/review_item_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace core::reviews {

namespace {

std::string ReadTextFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Could not open " + path.string();
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        error = "Could not read " + path.string();
        return {};
    }
    return buffer.str();
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& content, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Could not create " + path.parent_path().string() + ": " + ec.message();
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        error = "Could not write " + path.string();
        return false;
    }
    file << content;
    if (!file.good()) {
        error = "Could not finish writing " + path.string();
        return false;
    }
    return true;
}

} // namespace

void ReviewItemManager::SetFilePath(std::filesystem::path file_path) {
    file_path_ = std::move(file_path);
}

bool ReviewItemManager::Load(std::string& error) {
    items_.clear();
    element_states_.clear();
    if (file_path_.empty()) {
        error = "Review item file path is not set.";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(file_path_, ec)) {
        error = "Review item file does not exist: " + file_path_.string();
        return false;
    }
    return DeserializeReviewItems(ReadTextFile(file_path_, error), items_, element_states_, error);
}

bool ReviewItemManager::Save(std::string& error) const {
    if (file_path_.empty()) {
        error = "Review item file path is not set.";
        return false;
    }
    return WriteTextFile(file_path_, SerializeReviewItems(items_, element_states_), error);
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