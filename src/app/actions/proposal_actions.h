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

namespace review {
struct SuggestedDraftGroup;
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
    void CreateAiGenerated(const AiReviewProposalSuggestionsEvent& event);
    void CancelActive();
    bool AddChildToSelected(core::NewElementKind kind);
    bool AddTopGoal();
    void RemoveSelected(core::RemoveMode mode);

private:
    // Applies one suggested group to the draft document (ADR 0016), with its
    // provenance, and records it in the group's ledger. False, with `error`,
    // when the document refuses it; nothing is then applied.
    bool StageSuggestionInDraftDocument(const review::SuggestedDraftGroup& group,
                                        const std::string& group_id,
                                        std::string& error);

    AppRuntimeState& state_;
};

} // namespace app::actions
