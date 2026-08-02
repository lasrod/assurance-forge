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
#include <unordered_set>
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

// Human-readable, exactly deduplicated attribution for selected groups.
// Labels are compared as complete values: "AI" and "SCCG AI Review" name two
// contributors and must both survive into the audited promotion command.
std::string DraftPromotionAuthor(const DraftWorkspace& workspace,
                                 const std::vector<std::string>& group_ids,
                                 const std::string& fallback = "Working draft");

// Compiles every active group in `workspace`.
//
// `create_ref` values are namespaced per group before concatenation: two sources
// that independently chose `$goal` are not the same element, and merging them
// would silently fuse two proposed claims into one. This is a real collision --
// nothing coordinates the names two clients pick.
CompiledDraftPromotion CompileWorkspacePromotion(const DraftWorkspace& workspace, const std::string& author_name);

// Compiles the named groups only, in sequence order.
//
// The caller is expected to have closed `group_ids` over dependencies already --
// this does not do it, because widening a selection silently is the failure the
// closure exists to prevent, and the widening has to be shown to the user before
// it happens rather than performed inside a compile step.
CompiledDraftPromotion CompileSelectedPromotion(const DraftWorkspace& workspace,
                                                const std::vector<std::string>& group_ids,
                                                const std::string& author_name);

// Whether promoting `group_ids` would leave a coherent argument, checked before
// anything is written.
struct DraftPromotionPlan {
    bool ok = false;
    std::string error;
    // The groups that will actually be promoted: the selection closed over its
    // dependencies. Shown to the user before they commit.
    std::vector<std::string> closure;
    // Groups in the closure that the user did not pick, so the UI can say "this
    // also accepts ..." rather than quietly taking more than was asked for.
    std::vector<std::string> added_by_closure;
    // The accepted argument as it would be after promotion.
    core::AssuranceCase promoted_model;
    CompiledDraftPromotion compiled;
};

// Materializes the selection against the accepted baseline **and** the remaining
// groups against the prospective new baseline, and reports whether both hold.
//
// Both halves matter. A selection that applies cleanly can still leave the rest
// of the draft referring to something it removed, and discovering that after the
// accepted SACM had changed would mean a safety argument mutated into a state
// nobody chose. So this runs first and promotion is refused on failure.
DraftPromotionPlan PlanDraftPromotion(const DraftWorkspace& workspace,
                                      const core::AssuranceCase& accepted,
                                      const std::vector<std::string>& selection,
                                      const std::string& author_name,
                                      const std::unordered_set<std::string>& authoritative_identities = {});

} // namespace core::drafts
