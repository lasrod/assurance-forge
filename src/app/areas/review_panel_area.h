#pragma once

#include "core/problems/problem_item.h"
#include "core/reviews/review_item.h"

#include <functional>
#include <string>

namespace app {
struct AppRuntimeState;
}

namespace app::areas {

struct ReviewPanelAreaCallbacks {
    std::function<bool()> ensure_review_item_storage;
    std::function<void(const std::string&)> set_status;
    std::function<bool(const core::reviews::ReviewItem&)> create_proposed_change;
    std::function<bool(const core::reviews::ReviewItem&)> save_proposal;
    std::function<bool(const core::reviews::ReviewItem&)> edit_proposal;
    std::function<bool(const std::string&)> preview_proposal_by_id;
    std::function<void(const core::reviews::ReviewItem&)> apply_proposal;
    std::function<void(const core::reviews::ReviewItem&)> delete_proposal;
    std::function<bool(const core::reviews::ReviewItem&)> resolve_review_item;
    std::function<void(const core::reviews::ReviewItem&)> delete_review_item;
    std::function<void(const core::ProblemItem&)> quick_fix_problem;
    std::function<bool(const std::string&, bool)> set_manual_review_ok;
};

void RenderReviewPanelContent(AppRuntimeState& state, const ReviewPanelAreaCallbacks& callbacks);

} // namespace app::areas
