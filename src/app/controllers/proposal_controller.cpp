#include "app/controllers/proposal_controller.h"

#include "core/reviews/review_proposal_factory.h"

#include <utility>

namespace app::controllers {

bool ProposalController::IsCanvasActive() const {
    return preview_active || creator_active;
}

bool ProposalController::HasActiveDraftForItem(const std::string& review_item_id) const {
    return creator_active && draft.review_item_id == review_item_id;
}

bool ProposalController::CanSaveActiveDraft() const {
    return creator_active && !draft.operations.empty();
}

size_t ProposalController::ActiveOperationCount() const {
    return draft.operations.size();
}

void ProposalController::BeginDraft(const core::reviews::ReviewItem& item,
                                    const parser::AssuranceCase& model,
                                    const parser::SacmElement& anchor,
                                    const std::string& reviewer_name) {
    draft = core::reviews::BuildDraftReviewProposal(item, model, anchor);
    if (!reviewer_name.empty())
        draft.author_name = reviewer_name;
    creator_active = true;
    creator_generated_ids.clear();
}

void ProposalController::BeginEditDraft(core::reviews::ReviewProposal proposal, const std::string& reviewer_name) {
    draft = std::move(proposal);
    if (!reviewer_name.empty())
        draft.author_name = reviewer_name;
    creator_active = true;
    creator_generated_ids.clear();
}

void ProposalController::ClearActiveState() {
    creator_active = false;
    draft = {};
    creator_generated_ids.clear();
    creator_preview_refresh_pending = false;
    creator_pending_select_create_ref.reset();
    creator_pending_clear_selection = false;
    preview_active = false;
    preview_id.clear();
    preview_model = {};
}

bool ProposalController::ClosePreviewIfOpen(const std::string& proposal_id) {
    if (!preview_active || preview_id != proposal_id)
        return false;

    preview_active = false;
    preview_id.clear();
    preview_model = {};
    return true;
}

} // namespace app::controllers