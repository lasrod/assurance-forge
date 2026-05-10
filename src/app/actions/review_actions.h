#pragma once

#include "core/reviews/review_item.h"

#include <string>

namespace app { struct AppRuntimeState; }

namespace app::actions {

class ReviewActions {
public:
    explicit ReviewActions(AppRuntimeState& state);

    bool DeleteProposalPatchFile(const std::string& proposal_id, std::string& error);
    void CloseProposalPreviewIfOpen(const std::string& proposal_id);
    void BeginDeleteReviewItem(const core::reviews::ReviewItem& item);
    bool DeleteReviewItem(const core::reviews::ReviewItem& item);
    bool DeleteProposalForReviewItem(const core::reviews::ReviewItem& item);
    bool ResolveReviewItem(const core::reviews::ReviewItem& item, const std::string& updated_utc);

private:
    AppRuntimeState& state_;
};

} // namespace app::actions
