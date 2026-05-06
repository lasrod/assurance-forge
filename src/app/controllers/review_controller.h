#pragma once

#include "app/app_events.h"
#include "core/project_model.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_item_manager.h"

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
    AppEvents& events_;
    core::reviews::ReviewItemManager manager_;
    bool dirty_ = false;
    bool show_delete_confirm_ = false;
    core::reviews::ReviewItem pending_delete_item_;
};

} // namespace app::controllers