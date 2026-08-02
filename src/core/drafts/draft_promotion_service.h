#pragma once

// Turning selected draft groups into one thing the ordinary apply path can take.
//
// Promotion is the single point at which proposed work becomes accepted
// argument, and it deliberately reuses `ReviewProposal` and
// `ApplyProposalCommand` rather than introducing a promotion-specific mutation.
// That reuse is the guarantee: a draft cannot make a change the application
// could not make itself, and the result is audited, replayable and undoable
// exactly like an edit made with the mouse.
//
// Only a person reaches this. There is no MCP tool and no AI response that
// promotes (ADR 0009).

#include "core/drafts/draft_workspace.h"
#include "core/reviews/review_proposal.h"

#include <map>
#include <string>
#include <vector>

namespace core::drafts {

struct CompiledDraftPromotion {
    bool success = false;
    std::string error;

    // Every selected group's operations, concatenated in sequence order.
    reviews::ReviewProposal proposal;
    // `create_ref` -> element id for the whole selection, so the apply keeps the
    // identities the draft was shown under instead of allocating new ones.
    std::map<std::string, std::string> identities;

    // The groups this compilation covers, in the order they were applied.
    std::vector<std::string> group_ids;
    // Contributing source labels, deduplicated, for the audit record. A reader of
    // the log a year later needs to know an AI wrote this, and which one.
    std::vector<std::string> source_labels;
};

// Compiles every active group in `workspace`.
//
// `create_ref` values are namespaced per group before concatenation: two sources
// that independently chose `$goal` are not the same element, and merging them
// would silently fuse two proposed claims into one. This is a real collision --
// nothing coordinates the names two clients pick.
CompiledDraftPromotion CompileWorkspacePromotion(const DraftWorkspace& workspace, const std::string& author_name);

} // namespace core::drafts
