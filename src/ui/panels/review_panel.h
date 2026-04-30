#pragma once

#include "core/reviews/review_item.h"

#include <functional>
#include <string>
#include <vector>

namespace ui::panels {

struct ReviewPanelModel {
    std::string selected_element_id;
    std::vector<core::ReviewItem> review_items;
    bool has_project = false;
};

struct ReviewPanelCallbacks {
    std::function<void(const std::string& title, const std::string& message)> add_review_item;
    std::function<void(const core::ReviewItem& item)> create_proposed_change;
    std::function<void(const core::ReviewItem& item)> preview_proposal;
    std::function<void(const core::ReviewItem& item)> apply_proposal;
    std::function<void(const core::ReviewItem& item)> delete_proposal;
};

void ShowReviewPanel(const ReviewPanelModel& model, const ReviewPanelCallbacks& callbacks);

}  // namespace ui::panels