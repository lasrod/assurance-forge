#pragma once

#include "core/changesets/change_set.h"
#include "core/problems/problem_item.h"
#include "core/reviews/review_item.h"

#include <functional>
#include <string>
#include <vector>

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
    std::function<void(const std::string& element_id)> accept_translation_review;
    std::function<bool(const std::string&, bool)> set_manual_review_ok;
    // The only path from an agent's staged work into the safety case, and a
    // person's action. No tool an agent can call reaches it.
    std::function<void(const std::string& change_set_id)> accept_agent_change_set;
    std::function<void(const std::string& change_set_id)> reject_agent_change_set;
};

void RenderReviewPanelContent(AppRuntimeState& state, const ReviewPanelAreaCallbacks& callbacks);

// The SCCG findings against a change set, as the reviewer reads them on it.
//
// Exposed for the same reason the canvas projection is: the checks were tested,
// the panel that renders them was written, and nothing connected the two -- so
// the section could never appear and no test noticed, because no test crossed
// the seam.
std::vector<std::string> DescribeStagedSccgFindings(const core::changesets::ChangeSetDiff& diff);

} // namespace app::areas
