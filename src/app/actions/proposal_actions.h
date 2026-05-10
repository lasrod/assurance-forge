#pragma once

#include "core/element_factory.h"
#include "core/reviews/review_item.h"
#include "core/tree_editing.h"

#include <string>

namespace app {

struct AppRuntimeState;

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
    void CancelActive();
    bool AddChildToSelected(core::NewElementKind kind);
    bool AddTopGoal();
    void RemoveSelected(core::RemoveMode mode);

private:
    AppRuntimeState& state_;
};

} // namespace app
