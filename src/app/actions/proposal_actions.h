#pragma once

#include "app/app_events.h"
#include "core/element_factory.h"
#include "core/reviews/review_item.h"
#include "core/tree_editing.h"

#include <string>
#include <vector>

namespace app {
struct AppRuntimeState;
}

namespace app::actions {

class ProposalActions {
public:
    explicit ProposalActions(AppRuntimeState& state);

    bool RefreshCreatorPreview();
    void ProcessPendingCreatorPreviewRefresh();
    bool BeginForReviewItem(const core::reviews::ReviewItem& item);
    bool BeginEditForReviewItem(const core::reviews::ReviewItem& item);
    bool BeginEditById(const std::string& proposal_id);
    bool PreviewById(const std::string& proposal_id);
    bool SaveActive(const core::reviews::ReviewItem& item);
    bool ApplyReviewProposal(const core::reviews::ReviewItem& item);
    void CreateAiGenerated(const std::vector<AiReviewProposalSuggestion>& suggestions);
    void CancelActive();
    bool AddChildToSelected(core::NewElementKind kind);
    bool AddTopGoal();
    void RemoveSelected(core::RemoveMode mode);

private:
    AppRuntimeState& state_;
};

} // namespace app::actions
