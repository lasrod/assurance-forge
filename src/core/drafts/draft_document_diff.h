#pragma once

// What a draft changes, computed by comparing two documents (ADR 0016).
//
// Under ADR 0009/0010 this answer came from replaying a group's operations and
// recording what each one touched. The draft is now a SACM document in its own
// right, so the answer is a comparison instead: an element the draft has and the
// accepted argument does not is an addition, and so on. Nothing has to be
// inferred from an operation log, and the result cannot disagree with the model
// it describes -- it is derived from it.
//
// Deliberately over the PROJECTIONS rather than the library documents. The
// projection is what the canvas, the navigator and the inspector draw, so a
// difference reported here is a difference the user can actually see; and a
// document-level comparison would report structural noise (document order, a
// package's element list) that means nothing to a reader of the argument.

#include "core/drafts/draft_change_index.h" // DraftElementChange
#include "core/sacm_model.h"

#include <string>
#include <vector>

namespace core::drafts {

// One element's net difference between the accepted argument and the draft.
struct DraftDocumentChange {
    std::string element_id;
    DraftElementChange change = DraftElementChange::Unchanged;
    // For a Modified element, the POD field names that differ ("content",
    // "name", "source_refs", ...), sorted. Named rather than counted because the
    // inspector shows the reviewer which part of an element a contributor
    // touched, and "modified" alone does not distinguish a reworded claim from a
    // re-pointed relationship.
    std::vector<std::string> fields;
};

struct DraftDocumentDiff {
    // Sorted by element id, so the same draft always reports the same order.
    std::vector<DraftDocumentChange> changes;
    // The full accepted elements the draft removes. A removed element is absent
    // from the draft by definition, so a renderer that wants to show what is
    // being deleted needs it from here.
    std::vector<core::SacmElement> removed;

    int added_count = 0;
    int modified_count = 0;
    int removed_count = 0;

    const DraftDocumentChange* Find(const std::string& element_id) const;

    bool touches_anything() const {
        return added_count > 0 || modified_count > 0 || removed_count > 0;
    }
};

// Compares `accepted` against `draft`, pairing elements by id.
//
// Ids are the pairing key because they are what every other part of the
// application addresses an element by, and because the draft is a copy of the
// accepted document -- an element that survives editing keeps the id it was
// copied with. An id reused for a different element is not a case this
// distinguishes, and cannot arise while the draft descends from the accepted
// document.
DraftDocumentDiff DiffAcceptedAgainstDraft(const core::AssuranceCase& accepted, const core::AssuranceCase& draft);

// The comparison, expressed as the change index the canvas already consumes.
//
// A bridge, not a second representation: the canvas decorations, the "changes
// only" view and the tombstone reinsertion were all written against
// `DraftChangeIndex` when a draft was a list of operations, and they ask it only
// what changed and what was removed -- both of which the comparison answers.
//
// The per-group `contributions` are empty, and cannot be otherwise: a document
// records what it holds, not who put it there. Provenance travels with the
// element as tagged values (ADR 0016) and is read from the element, not from
// here.
DraftChangeIndex ChangeIndexFromDiff(const DraftDocumentDiff& diff);

// The POD fields that differ between two projections of the same element,
// sorted. Empty when they are equivalent.
//
// Exposed because the same comparison answers "did this edit change anything?"
// for a caller deciding whether to record a change at all.
std::vector<std::string> ChangedElementFields(const core::SacmElement& accepted, const core::SacmElement& draft);

} // namespace core::drafts
