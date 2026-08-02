#pragma once

// What the working draft does to each element, and which group did it.
//
// The canvas needs the first half to decorate a node, and the inspector needs
// the second: an element can be created by an MCP client, reworded by an SCCG
// review and then edited by the user, and a reviewer approving that element is
// entitled to see all three contributions in order rather than the net result
// alone.
//
// **Relationships are elements here**, because they are `SacmElement` values in
// the model, so a new or removed `assertedInference` appears in the index the
// same way a claim does. Note that relationship ids are *generated during
// materialization* and are not pinned the way element ids are: the operation
// vocabulary addresses a relationship by its endpoints, never by id, so nothing
// references one across a rebuild. A consumer that wants a stable handle to a
// relationship should key on (type, source, target) rather than on the id in
// this index.

#include "core/sacm_model.h"

#include <map>
#include <string>
#include <vector>

namespace core::drafts {

enum class DraftElementChange {
    Unchanged,
    Added,
    Modified,
    Removed,
};

const char* DraftElementChangeToString(DraftElementChange change);

// One group's effect on one element.
struct DraftElementContribution {
    std::string group_id;
    DraftElementChange change = DraftElementChange::Unchanged;
};

struct DraftElementEntry {
    // The net effect against the accepted baseline, which is what the canvas
    // draws. A claim a group created and a later group reworded is `Added`, not
    // `Modified`: to the accepted argument it is simply new.
    DraftElementChange change = DraftElementChange::Unchanged;
    // Every contribution, in materialization order.
    std::vector<DraftElementContribution> contributions;
};

struct DraftChangeIndex {
    std::map<std::string, DraftElementEntry> elements;
    // Elements the draft removes. Absent from the working model by definition,
    // so a renderer that wants to ghost them needs them from here.
    std::vector<core::SacmElement> removed;

    int added_count = 0;
    int modified_count = 0;
    int removed_count = 0;

    const DraftElementEntry* Find(const std::string& element_id) const;

    // Every element the draft touches, sorted, so a caller that feeds these to a
    // check gets the same input on every materialization.
    std::vector<std::string> ChangedElementIds() const;

    // Groups that contributed to `element_id`, in materialization order.
    std::vector<std::string> ContributingGroupIds(const std::string& element_id) const;

    bool touches_anything() const {
        return added_count > 0 || modified_count > 0 || removed_count > 0;
    }
};

// Records what one group did, by comparing the model immediately before it with
// the model immediately after it.
//
// Attribution has to be done step by step: the net diff against the accepted
// baseline cannot say which of three groups reworded a claim.
void RecordGroupContributions(DraftChangeIndex& index,
                              const std::string& group_id,
                              const core::AssuranceCase& before,
                              const core::AssuranceCase& after);

// Computes the net effect of the whole draft and the counts. Called once, after
// every group has been recorded.
void FinalizeChangeIndex(DraftChangeIndex& index,
                         const core::AssuranceCase& accepted,
                         const core::AssuranceCase& working);

} // namespace core::drafts
