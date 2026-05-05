#include "app/controllers/review_controller.h"

#include "core/project_service.h"
#include "core/reviews/review_item.h"

#include <filesystem>
#include <string>
#include <utility>

namespace app::controllers {

ReviewController::ReviewController(AppEvents& events) : events_(events) {}

bool ReviewController::ConfigureStorage(const std::filesystem::path& review_path, std::string& error) {
    if (dirty_ && !manager_.FilePath().empty() && manager_.FilePath() == review_path) {
        error.clear();
        return true;
    }

    manager_.SetFilePath(review_path);
    if (manager_.Load(error))
        return true;

    std::error_code exists_error;
    if (!std::filesystem::exists(review_path, exists_error)) {
        manager_.Clear();
        manager_.SetFilePath(review_path);
        return true;
    }

    return false;
}

void ReviewController::ClearStorage() {
    manager_.Clear();
    dirty_ = false;
    show_delete_confirm_ = false;
    pending_delete_item_ = {};
}

bool ReviewController::SaveIfDirty(core::AssuranceProject& project, std::string& error) {
    if (!dirty_)
        return true;
    if (manager_.FilePath().empty()) {
        error = "review storage path is not set.";
        return false;
    }

    std::filesystem::path requested_name = manager_.FilePath().filename();
    if (requested_name.empty())
        requested_name = "review-items.af.json";

    core::ProjectFileEntry entry;
    const std::string content = core::reviews::SerializeReviewItems(manager_.GetItems());
    if (!core::ProjectService::SaveReviewItemsFile(project, requested_name.string(), content, entry, error)) {
        return false;
    }

    manager_.SetFilePath(project.rootPath / entry.relativePath);
    dirty_ = false;
    return true;
}

bool ReviewController::IsDirty() const {
    return dirty_;
}

void ReviewController::ClearDirty() {
    dirty_ = false;
}

void ReviewController::MarkDirty() {
    dirty_ = true;
    events_.Emit(ReviewItemsDirtyEvent{});
}

const std::filesystem::path& ReviewController::FilePath() const {
    return manager_.FilePath();
}

const std::vector<core::reviews::ReviewItem>& ReviewController::Items() const {
    return manager_.GetItems();
}

std::vector<core::reviews::ReviewItem> ReviewController::ItemsForElement(const std::string& element_id) const {
    return manager_.GetItemsForElement(element_id);
}

std::optional<core::reviews::ReviewItem> ReviewController::GetItemById(const std::string& item_id) const {
    return manager_.GetItemById(item_id);
}

bool ReviewController::AddManualItem(core::reviews::ReviewItem item) {
    if (!AddOrUpdateItem(std::move(item))) {
        events_.Emit(StatusMessageEvent{"Could not add review comment."});
        return false;
    }
    events_.Emit(StatusMessageEvent{"Review comment added."});
    return true;
}

bool ReviewController::AddOrUpdateItem(core::reviews::ReviewItem item) {
    if (!manager_.AddOrUpdateItem(std::move(item)))
        return false;
    MarkDirty();
    return true;
}

bool ReviewController::SetProposal(const std::string& item_id, const std::string& proposal_id) {
    if (!manager_.SetProposal(item_id, proposal_id))
        return false;
    MarkDirty();
    return true;
}

bool ReviewController::ClearProposal(const std::string& item_id) {
    if (!manager_.ClearProposal(item_id))
        return false;
    MarkDirty();
    return true;
}

void ReviewController::BeginDeleteReviewItem(const core::reviews::ReviewItem& item, bool proposal_creator_active) {
    show_delete_confirm_ = false;
    pending_delete_item_ = {};

    if (proposal_creator_active) {
        events_.Emit(StatusMessageEvent{"Save or discard the active proposal before deleting review comments."});
        return;
    }
    if (item.proposal_id.has_value()) {
        pending_delete_item_ = item;
        show_delete_confirm_ = true;
    }
}

bool ReviewController::DeleteReviewItem(const core::reviews::ReviewItem& item,
                                        bool proposal_creator_active,
                                        bool has_project,
                                        const DeleteLinkedProposal& delete_linked_proposal,
                                        const CloseProposalPreview& close_preview) {
    if (proposal_creator_active) {
        events_.Emit(StatusMessageEvent{"Save or discard the active proposal before deleting review comments."});
        return false;
    }
    if (!has_project) {
        events_.Emit(StatusMessageEvent{"Open a project before deleting review comments."});
        return false;
    }

    if (item.proposal_id.has_value()) {
        std::string error;
        if (!delete_linked_proposal || !delete_linked_proposal(item.proposal_id.value(), error)) {
            events_.Emit(StatusMessageEvent{"Review comment delete failed while deleting proposal: " + error});
            return false;
        }
        if (close_preview)
            close_preview(item.proposal_id.value());
    }

    if (!manager_.RemoveItem(item.id)) {
        events_.Emit(StatusMessageEvent{"Review comment was already removed."});
        return false;
    }

    MarkDirty();
    events_.Emit(StatusMessageEvent{item.proposal_id.has_value() ? "Deleted review comment and proposed change."
                                                                 : "Deleted review comment."});
    return true;
}

bool ReviewController::ResolveReviewItem(const core::reviews::ReviewItem& item,
                                         bool proposal_creator_active,
                                         bool has_project,
                                         const std::string& updated_utc) {
    if (proposal_creator_active) {
        events_.Emit(StatusMessageEvent{"Save or discard the active proposal before resolving review comments."});
        return false;
    }
    if (item.status == core::reviews::ReviewItemStatus::Resolved) {
        events_.Emit(StatusMessageEvent{"Review comment is already resolved."});
        return true;
    }
    if (!has_project) {
        events_.Emit(StatusMessageEvent{"Open a project before resolving review comments."});
        return false;
    }

    core::reviews::ReviewItem updated = item;
    updated.status = core::reviews::ReviewItemStatus::Resolved;
    updated.updated_utc = updated_utc;
    if (!manager_.AddOrUpdateItem(std::move(updated))) {
        events_.Emit(StatusMessageEvent{"Could not resolve review comment."});
        return false;
    }

    MarkDirty();
    events_.Emit(StatusMessageEvent{"Review comment resolved."});
    return true;
}

bool ReviewController::ShouldShowDeleteConfirm() const {
    return show_delete_confirm_;
}

const core::reviews::ReviewItem& ReviewController::PendingDeleteReviewItem() const {
    return pending_delete_item_;
}

void ReviewController::CancelDeleteReviewItem() {
    show_delete_confirm_ = false;
    pending_delete_item_ = {};
}

} // namespace app::controllers