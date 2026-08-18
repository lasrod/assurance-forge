#pragma once

// Applying a contributor's operations directly to the draft document (ADR 0016).
//
// This is where "an MCP client cannot ask for something the application cannot
// do" stops being a validation rule and becomes a fact about the code. Each
// operation is handed to the same `sacm_adapter` seam the application uses on
// the accepted document, so the model that will hold the change is the one that
// accepts or refuses it, in the call that made it. There is no flat intermediate
// model to disagree with the document, because there is no flat intermediate
// model.
//
// It replaces `ReviewProposalPatchService` on the draft path, which applied
// operations to a `core::AssuranceCase` that carries `name`/`content`/
// `description` on every element regardless of kind -- and so accepted fourteen
// distinct shapes the seams later refused, each of them producing a draft the
// user could see and never accept.

#include "core/reviews/review_proposal.h"
#include "sacm_adapter/library_load.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace core::drafts {

struct DraftOperationResult {
    bool applied = false;
    // Why the batch was refused, phrased for the agent that sent it: what was
    // wrong and, where there is one, the thing to do instead. A refusal an agent
    // cannot act on costs a conversation turn and teaches it nothing.
    std::string error;
    // The 1-based position of the operation that failed, so a client staging ten
    // operations is told which one. Zero when the batch succeeded.
    std::size_t failed_operation = 0;
    // `create_ref` -> the id the document allocated, for everything this batch
    // created. Real ids, allocated when the element was created rather than
    // reserved and pinned: there is no later materialization that could allocate
    // a different one.
    std::map<std::string, std::string> created_ids;
};

// Applies `operations` to `document` as one all-or-nothing batch.
//
// Atomic by construction: the batch is applied to a copy, and the copy replaces
// `document` only once every operation has succeeded. A batch that fails leaves
// the draft exactly as it was, so a client whose third operation is refused does
// not have to reason about whether its first two survived.
//
// `anchor_element_id` decides which ArgumentPackage newly created elements are
// filed in -- the package owning the anchor, falling back to the document's
// first. A flat model records no package, and guessing would put a proposed
// claim in the wrong package of a multi-package case.
DraftOperationResult ApplyOperationsToDraftDocument(sacm_adapter::LibraryDocument& document,
                                                    const std::vector<reviews::PatchOperation>& operations,
                                                    const std::string& anchor_element_id = {});

} // namespace core::drafts
