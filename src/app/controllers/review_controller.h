#pragma once

#include "app/app_events.h"
#include "core/project_model.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_item_manager.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace app::controllers {

enum class ElementReviewStatus {
    NotReviewed,
    Passed,
    OpenItems,
    Failed,
};

class ReviewController {
public:
    using DeleteLinkedProposal = std::function<bool(const std::string& proposal_id, std::string& error)>;
    using CloseProposalPreview = std::function<void(const std::string& proposal_id)>;

    explicit ReviewController(AppEvents& events);

    bool ConfigureStorage(const std::filesystem::path& review_path, std::string& error);
    void ClearStorage();
    bool SaveIfDirty(core::AssuranceProject& project, std::string& error);

    // Reloads review items when another process has rewritten the file since we
    // last touched it, and reports whether it did so the caller can refresh
    // derived views.
    //
    // Review items were loaded once, on project open. Anything writing them from
    // outside the running app -- the MCP server saving a proposal, most obviously
    // -- was therefore invisible until the project was reopened, which is not a
    // thing a user should have to know to do.
    //
    // Self-throttled, so the frame loop can call it unconditionally. Declines to
    // reload while there are unsaved review edits: losing what the user typed is
    // worse than showing an incoming comment a little late.
    bool ReloadIfChangedExternally();

    bool IsDirty() const;
    void ClearDirty();
    void MarkDirty();

    const std::filesystem::path& FilePath() const;
    const std::vector<core::reviews::ReviewItem>& Items() const;
    std::vector<core::reviews::ReviewItem> ItemsForElement(const std::string& element_id) const;
    std::optional<core::reviews::ReviewItem> GetItemById(const std::string& item_id) const;
    const core::reviews::ElementReviewStateMap& ElementReviewStates() const;
    core::reviews::ElementReviewState ElementReviewStateForElement(const std::string& element_id) const;
    ElementReviewStatus StatusForElement(const std::string& element_id, bool has_blocking_problem = false) const;

    bool AddManualItem(core::reviews::ReviewItem item);
    bool AddOrUpdateItem(core::reviews::ReviewItem item);
    size_t ClearAiReviewItemsForElementAndPrefix(const std::string& element_id, const std::string& id_prefix);
    bool SetProposal(const std::string& item_id, const std::string& proposal_id);
    bool ClearProposal(const std::string& item_id);
    bool AddDraftGroup(const std::string& item_id, const std::string& group_id);
    bool SetManualReviewOk(const std::string& element_id,
                           bool manual_ok,
                           const std::string& reviewer_name,
                           const std::string& updated_utc);
    bool SetAiReviewOutcome(const std::string& element_id,
                            bool ai_ok,
                            bool failed,
                            const std::string& review_profile_id,
                            const std::string& review_profile_name,
                            const std::string& message,
                            const std::string& updated_utc);

    void BeginDeleteReviewItem(const core::reviews::ReviewItem& item, bool proposal_creator_active);
    bool DeleteReviewItem(const core::reviews::ReviewItem& item,
                          bool proposal_creator_active,
                          bool has_project,
                          const DeleteLinkedProposal& delete_linked_proposal,
                          const CloseProposalPreview& close_preview);
    bool ResolveReviewItem(const core::reviews::ReviewItem& item,
                           bool proposal_creator_active,
                           bool has_project,
                           const std::string& updated_utc);

    bool ShouldShowDeleteConfirm() const;
    const core::reviews::ReviewItem& PendingDeleteReviewItem() const;
    void CancelDeleteReviewItem();

private:
    // Identity of the review file as we last saw it. Size as well as mtime
    // because a same-second rewrite is entirely plausible when another process
    // is doing the writing.
    struct FileStamp {
        bool exists = false;
        std::filesystem::file_time_type mtime{};
        std::uintmax_t size = 0;

        bool operator==(const FileStamp& other) const {
            return exists == other.exists && mtime == other.mtime && size == other.size;
        }
    };

    // Records the file as it stands now, so our own writes are not mistaken for
    // somebody else's.
    void RestampReviewFile();

    AppEvents& events_;
    core::reviews::ReviewItemManager manager_;
    bool dirty_ = false;
    bool show_delete_confirm_ = false;
    core::reviews::ReviewItem pending_delete_item_;
    FileStamp stamp_;
    std::chrono::steady_clock::time_point last_external_check_{};
};

} // namespace app::controllers
