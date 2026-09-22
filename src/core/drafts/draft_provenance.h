#pragma once

// Who contributed what to a working draft, recorded on the elements themselves
// (ADR 0016, #409).
//
// Provenance is a set of vendor TaggedValues (clause 8.12) on each element a
// contributor added or changed, under `kDraftProvenanceTagPrefix`:
//
//   assuranceForge.draft.<contribution>.source     mcp | sccg_ai_review | human
//   assuranceForge.draft.<contribution>.label      "Claude Code 1.2", or the reviewer's name
//   assuranceForge.draft.<contribution>.session    MCP session or review-run identity
//   assuranceForge.draft.<contribution>.title      what the contribution says it is for
//   assuranceForge.draft.<contribution>.rationale  why
//
// The contribution is part of the KEY, not the value, so two contributors to the
// same element each keep their own record rather than the second overwriting the
// first. That is the property the parallel change-group index could not offer:
// tags travel with the element and cannot describe an argument other than the
// one they sit in.
//
// Accept strips every tag under the prefix, so none of this reaches an accepted
// safety case.
//
// **A removed element carries no provenance.** It is not in the draft to carry a
// tag. Who removed it is not recorded here; the diff still reports that it was
// removed.

#include "core/drafts/draft_workspace.h" // DraftSource
#include "sacm_adapter/library_load.h"

#include <string>
#include <vector>

namespace core::drafts {

// The TaggedValue key prefix every piece of draft provenance is filed under, and
// the one `DraftDocumentStore::AcceptInto` strips. A single constant because the
// writer and the stripper disagreeing would either lose provenance while a draft
// is open or leak it into an accepted safety case.
inline constexpr char kDraftProvenanceTagPrefix[] = "assuranceForge.draft.";

struct DraftProvenance {
    // Names the contribution inside the tag key, so it must not contain '.'.
    // An MCP contribution uses its change-group id; hand edits use
    // `HumanContributionId`.
    std::string contribution_id;
    DraftSource source = DraftSource::Human;
    std::string label;
    std::string session_id;
    std::string title;
    std::string rationale;
};

// The contribution id for one person's hand edits: the same author always gets
// the same id, so their edits across a session read back as one contribution,
// and a second reviewer on the same draft gets a different one.
std::string HumanContributionId(const std::string& author);

// Writes `provenance` onto each of `element_ids`. Empty fields are not written.
// Rewriting a field this contribution already set on an element replaces it.
//
// Refuses without writing anything for a contribution id that is empty or
// contains '.', because the reader could not split such a key back apart. A
// failure part-way through leaves earlier elements tagged, so callers write to
// a scratch document they can abandon.
bool WriteDraftProvenance(sacm_adapter::LibraryDocument& document,
                          const std::vector<std::string>& element_ids,
                          const DraftProvenance& provenance,
                          std::string& error);

struct DraftContribution {
    DraftProvenance provenance;
    // The elements carrying this contribution's tags, sorted.
    std::vector<std::string> element_ids;
};

// Every contribution recorded in `document`, in the order each first appears in
// the document. A tag whose key does not parse as `<contribution>.<field>` is
// skipped rather than guessed at.
std::vector<DraftContribution> ReadDraftProvenance(const sacm_adapter::LibraryDocument& document);

} // namespace core::drafts
