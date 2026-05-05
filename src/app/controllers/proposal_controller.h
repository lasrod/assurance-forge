#pragma once

#include "core/element_factory.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_manager.h"
#include "parser/xml_parser.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace app::controllers {

class ProposalController {
public:
    core::reviews::ReviewProposalManager manager;

    bool preview_active = false;
    bool creator_active = false;
    parser::AssuranceCase preview_model;
    std::string preview_id;
    core::reviews::ReviewProposal draft;
    std::map<std::string, std::string> creator_generated_ids;
    bool creator_preview_refresh_pending = false;
    std::optional<std::string> creator_pending_select_create_ref;
    bool creator_pending_clear_selection = false;

    bool IsCanvasActive() const;
    bool HasActiveDraftForItem(const std::string& review_item_id) const;
    bool CanSaveActiveDraft() const;
    size_t ActiveOperationCount() const;

    void BeginDraft(const core::reviews::ReviewItem& item,
                    const parser::AssuranceCase& model,
                    const parser::SacmElement& anchor,
                    const std::string& reviewer_name);
    void BeginEditDraft(core::reviews::ReviewProposal proposal, const std::string& reviewer_name);
    void ClearActiveState();
    bool ClosePreviewIfOpen(const std::string& proposal_id);
};

} // namespace app::controllers