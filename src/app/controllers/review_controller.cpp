#include "app/controllers/review_controller.h"

#include "core/project_service.h"
#include "core/reviews/review_item.h"

#include <filesystem>
#include <string>
#include <utility>

namespace app::controllers {

namespace {

// How often the review file is stat'd. Slow enough to be free at frame rates,
// fast enough that a proposal arriving from another process feels immediate.
constexpr auto kExternalCheckInterval = std::chrono::milliseconds(750);

} // namespace

ReviewController::ReviewController(AppEvents& events) : events_(events) {}

void ReviewController::RestampReviewFile() {
    stamp_ = FileStamp{};
    const std::filesystem::path& path = manager_.FilePath();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return;
    }
    stamp_.exists = true;
    stamp_.mtime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        stamp_.exists = false;
        return;
    }
    stamp_.size = std::filesystem::file_size(path, ec);
    if (ec) {
        stamp_.size = 0;
    }
}

bool ReviewController::ReloadIfChangedExternally() {
    if (manager_.FilePath().empty()) {
        return false;
    }
    // Unsaved edits win. They exist only in memory, so reloading would discard
    // them silently; an incoming comment can wait until the user saves.
    if (dirty_) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if ((now - last_external_check_) < kExternalCheckInterval) {
        return false;
    }
    last_external_check_ = now;

    const FileStamp previous = stamp_;
    RestampReviewFile();
    if (stamp_ == previous) {
        return false;
    }

    std::string error;
    if (!manager_.Load(error)) {
        // A half-written file from a writer mid-flush reads as a parse failure.
        // Leaving the stamp as the failed one means we retry on its next change
        // rather than sitting on a corrupt read forever.
        return false;
    }
    return true;
}

bool ReviewController::ConfigureStorage(const std::filesystem::path& review_path, std::string& error) {
    if (dirty_ && !manager_.FilePath().empty() && manager_.FilePath() == review_path) {
        error.clear();
        return true;
    }

    manager_.SetFilePath(review_path);
    if (manager_.Load(error)) {
        RestampReviewFile();
        return true;
    }

    std::error_code exists_error;
    if (!std::filesystem::exists(review_path, exists_error)) {
        manager_.Clear();
        manager_.SetFilePath(review_path);
        RestampReviewFile();
        return true;
    }

    // A file that exists and will not parse. `Load` no longer empties itself on
    // failure, so say it here: these are a different project's review items and
    // keeping them would attach one project's comments to another's elements.
    manager_.Clear();
    manager_.SetFilePath(review_path);
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
    const std::string content =
        core::reviews::SerializeReviewItems(manager_.GetItems(), manager_.GetElementReviewStates());
    if (!core::ProjectService::SaveReviewItemsFile(project, requested_name.string(), content, entry, error)) {
        return false;
    }

    manager_.SetFilePath(project.rootPath / entry.relativePath);
    dirty_ = false;
    // Our own write, so record it as the baseline rather than detecting it as
    // somebody else's change on the next poll.
    RestampReviewFile();
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

const core::reviews::ElementReviewStateMap& ReviewController::ElementReviewStates() const {
    return manager_.GetElementReviewStates();
}

core::reviews::ElementReviewState ReviewController::ElementReviewStateForElement(const std::string& element_id) const {
    return manager_.GetElementReviewState(element_id);
}

ElementReviewStatus ReviewController::StatusForElement(const std::string& element_id, bool has_blocking_problem) const {
    const core::reviews::ElementReviewState state = manager_.GetElementReviewState(element_id);
    if (state.failed)
        return ElementReviewStatus::Failed;
    for (const core::reviews::ReviewItem& item : manager_.GetItemsForElement(element_id)) {
        if (item.status == core::reviews::ReviewItemStatus::Open)
            return ElementReviewStatus::OpenItems;
    }
    if (has_blocking_problem)
        return ElementReviewStatus::OpenItems;
    if (state.manual_ok || state.ai_ok)
        return ElementReviewStatus::Passed;
    return ElementReviewStatus::NotReviewed;
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

size_t ReviewController::ClearAiReviewItemsForElementAndPrefix(const std::string& element_id,
                                                               const std::string& id_prefix) {
    const size_t removed = manager_.RemoveItemsForElementSourceAndIdPrefix(
        element_id, core::reviews::ReviewItemSource::AIReview, id_prefix);
    if (removed > 0)
        MarkDirty();
    return removed;
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

bool ReviewController::SetManualReviewOk(const std::string& element_id,
                                         bool manual_ok,
                                         const std::string& reviewer_name,
                                         const std::string& updated_utc) {
    core::reviews::ElementReviewState state = manager_.GetElementReviewState(element_id);
    if (state.manual_ok == manual_ok && state.reviewed_by == reviewer_name && state.updated_utc == updated_utc)
        return true;
    state.manual_ok = manual_ok;
    state.reviewed_by = reviewer_name;
    state.updated_utc = updated_utc;
    if (!manager_.SetElementReviewState(element_id, std::move(state)))
        return false;
    MarkDirty();
    events_.Emit(StatusMessageEvent{manual_ok ? "Element marked OK manually." : "Manual review OK cleared."});
    return true;
}

bool ReviewController::SetAiReviewOutcome(const std::string& element_id,
                                          bool ai_ok,
                                          bool failed,
                                          const std::string& review_profile_id,
                                          const std::string& review_profile_name,
                                          const std::string& message,
                                          const std::string& updated_utc) {
    core::reviews::ElementReviewState state = manager_.GetElementReviewState(element_id);
    state.ai_ok = ai_ok;
    state.failed = failed;
    state.review_profile_id = review_profile_id;
    state.review_profile_name = review_profile_name;
    state.last_review_message = message;
    state.updated_utc = updated_utc;
    if (!manager_.SetElementReviewState(element_id, std::move(state)))
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