#include "app/controllers/proposal_controller.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace app::controllers {
namespace {

std::string NowUtcString() {
    using clock = std::chrono::system_clock;
    std::time_t now = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string GenerateReviewProposalId() {
    static unsigned long long counter = 0;
    auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "proposal-" << std::hex << ticks << "-" << ++counter;
    return out.str();
}

std::string TruncateForProblemMessage(const std::string& value, size_t limit = 400) {
    if (value.size() <= limit)
        return value;
    return value.substr(0, limit) + "...";
}

core::reviews::ReviewProposal BuildDraftReviewProposal(const core::reviews::ReviewItem& item,
                                                       const parser::AssuranceCase& model,
                                                       const parser::SacmElement& anchor) {
    core::reviews::ReviewProposal proposal;
    proposal.id = GenerateReviewProposalId();
    proposal.review_item_id = item.id;
    proposal.title = item.title.empty() ? "Proposed change" : item.title;
    proposal.summary = item.message.empty() ? "Draft proposed change." : TruncateForProblemMessage(item.message, 180);
    proposal.author_name = "Manual reviewer";
    proposal.created_utc = NowUtcString();
    proposal.anchor_element_id = anchor.id;
    proposal.affected_existing_element_ids = {anchor.id};
    proposal.base_model_hash = core::reviews::ComputeModelSemanticHash(model);
    proposal.base_element_hashes[anchor.id] = core::reviews::ComputeElementSemanticHash(anchor);
    return proposal;
}

} // namespace

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
    draft = BuildDraftReviewProposal(item, model, anchor);
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