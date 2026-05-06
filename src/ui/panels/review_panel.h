#pragma once

#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "core/problems/problem_item.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ui::panels {

struct ReviewGuidelineOption {
    std::string id;
    std::string category;
    std::string title;
};

struct ReviewPanelModel {
    std::string selected_element_id;
    std::vector<core::reviews::ReviewItem> review_items;
    std::vector<core::ProblemItem> problem_items;
    std::vector<ReviewGuidelineOption> guideline_options;
    std::string guideline_status;
    std::map<std::string, core::reviews::ProposalValidityResult> proposal_validity;
    std::string active_proposal_review_item_id;
    size_t active_proposal_operation_count = 0;
    bool active_proposal_can_save = false;
    bool has_project = false;
};

struct ReviewPanelCallbacks {
    std::function<void(
        const std::string& title, const std::string& message, const std::vector<std::string>& guideline_ids)>
        add_review_item;
    std::function<void(const core::reviews::ReviewItem& item)> create_proposed_change;
    std::function<void(const core::reviews::ReviewItem& item)> save_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> edit_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> preview_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> apply_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> delete_proposal;
    std::function<void(const core::reviews::ReviewItem& item)> resolve_review_item;
    std::function<void(const core::reviews::ReviewItem& item)> delete_review_item;
    std::function<void(const core::ProblemItem& problem)> delete_problem;
};

void ShowReviewPanel(const ReviewPanelModel& model, const ReviewPanelCallbacks& callbacks);

} // namespace ui::panels