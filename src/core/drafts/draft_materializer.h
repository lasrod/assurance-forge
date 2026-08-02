#pragma once

// Turns the accepted baseline plus the workspace's active groups into one
// complete assurance case.
//
// This is the object the previous design never had. A `ChangeSet` produced a
// preview of *itself* against the committed model, and a `ReviewProposal` file
// was a patch against the model it was written for -- so two proposals could
// each be individually valid and jointly incoherent, and nothing computed the
// join until a human accepted both. The working model is that join, and it is
// what the canvas draws, what an MCP client reads, what an SCCG review is asked
// about, and what the structural checks run over.
//
// **Materialization never touches the accepted model.** It works on a copy. A
// group that cannot be applied fails the whole materialization and names itself;
// it does not leave a half-applied model behind, because a half-applied safety
// argument is worse than no preview at all.

#include "core/drafts/draft_change_index.h"
#include "core/drafts/draft_workspace.h"
#include "core/problems/argument_cycles.h"
#include "core/problems/gsn_wellformedness.h"
#include "core/sccg/staged_checks.h"

#include <string>
#include <vector>

namespace core::drafts {

struct DraftMaterializationResult {
    bool success = false;
    std::string error;
    // Which group could not be applied. Empty when the failure is not
    // attributable to one -- the reviewer is told *which* proposal broke rather
    // than that "the draft" did.
    std::string failing_group_id;

    // The accepted baseline with every active group applied, in sequence order.
    // On failure this is the accepted baseline unchanged, so a caller that
    // renders it regardless shows accepted content rather than a partial draft.
    core::AssuranceCase working_model;

    DraftChangeIndex change_index;

    // Findings attributable to the draft, scoped to what it touched. An agent
    // should not be handed problems about parts of the argument it did not
    // write, and a group row should not display them.
    //
    // These are *not* the Problems panel's findings. That pipeline runs over
    // whatever model the application gives it, and gets the combination for free
    // once the working model is the model it is given.
    std::vector<core::sccg::StagedFinding> sccg_findings;
    std::vector<core::GsnFinding> gsn_findings;
    std::vector<core::ArgumentCycle> cycles;

    // True when materialization had to allocate element identities for a group
    // that did not have them yet. The caller owns persistence and must save, or
    // the identities are reallocated on the next run and proposed elements
    // rename underneath whoever was looking at them.
    bool allocated_identities = false;
};

// Materializes `workspace` against `accepted`.
//
// Takes the workspace by non-const reference because the first materialization
// of a group *allocates* its `create_ref` -> element id map and records it on
// the group. Every materialization after that replays those identities through
// `ReviewProposalPatchService::ApplyProposalWithIds` rather than generating new
// ones. Check `allocated_identities` and persist when it is set.
DraftMaterializationResult MaterializeDraft(DraftWorkspace& workspace, const core::AssuranceCase& accepted);

// Whether `operations` can be applied on top of everything already active in
// `workspace`. Used at staging time so a group never holds a patch that cannot
// be materialized -- which is what lets the canvas draw the working model at any
// moment.
//
// Does not modify `workspace`.
bool CanStageOperations(const DraftWorkspace& workspace,
                        const core::AssuranceCase& accepted,
                        const std::string& group_id,
                        const std::vector<reviews::PatchOperation>& operations,
                        std::string& error);

} // namespace core::drafts
