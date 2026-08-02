#pragma once

#include "core/reviews/review_proposal.h"

#include <map>
#include <string>
#include <unordered_set>

namespace core::reviews {

// Fills in `ids` for every create operation in `proposal` that it does not
// already name, allocating against `current_model`'s id space plus `reserved`.
//
// **Existing entries are never reassigned.** This is what lets a caller extend a
// patch with new create operations without renaming the elements the earlier
// ones produced -- the draft workspace needs exactly that, because
// materialization reruns whenever anything changes and an element that renames
// between frames breaks canvas selection and an agent's reference to what it was
// told it created.
//
// `reserved` carries ids claimed elsewhere but not yet present in
// `current_model`: identities pinned by draft groups that have not been applied
// at this point in the sequence. Without it, two groups can be handed the same
// id and the second fails to apply.
bool AllocateProposalIds(const ReviewProposal& proposal,
                         const parser::AssuranceCase& current_model,
                         const std::unordered_set<std::string>& reserved,
                         std::map<std::string, std::string>& ids,
                         std::string& error);

struct ApplyProposalResult {
    bool success = false;
    std::string error;
    std::map<std::string, std::string> generated_ids;
};

struct ProposalPreviewResult {
    bool success = false;
    std::string error;
    parser::AssuranceCase preview_model;
    std::map<std::string, std::string> generated_ids;
};

class ReviewProposalPatchService {
public:
    ProposalPreviewResult BuildPreviewModel(const ReviewProposal& proposal,
                                            const parser::AssuranceCase& current_model) const;

    ApplyProposalResult ApplyProposal(const ReviewProposal& proposal, parser::AssuranceCase& current_model) const;

    // Replay variant: apply the proposal using a caller-provided map of
    // `create_ref` → element id (typically captured into an audit payload
    // during the original apply and restored here). The patch service
    // skips its own deterministic id generation and uses the supplied
    // identities instead. Fails if any required `create_ref` is missing.
    ApplyProposalResult ApplyProposalWithIds(const ReviewProposal& proposal,
                                             parser::AssuranceCase& current_model,
                                             const std::map<std::string, std::string>& predetermined_ids) const;
};

} // namespace core::reviews
