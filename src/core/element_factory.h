#pragma once

#include <string>
#include <unordered_set>
#include <vector>
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace core {

enum class NewElementKind {
    Goal,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
};

// Add a new element of the given kind as a child of parent_id.
// Updates both the parser model (drives UI) and the sacm package (drives save).
// Returns true on success and writes the new element id to out_new_id.
// On failure, out_error contains a human-readable reason.
bool AddChildElement(parser::AssuranceCase& ac,
                     sacm::AssuranceCasePackage* pkg,
                     const std::string& parent_id,
                     NewElementKind kind,
                     std::string& out_new_id,
                     std::string& out_error);

// Count the number of descendant elements (children, grandchildren, ...) of
// the element with id `id`. Descendants are derived by walking relationship
// elements: an element X is a child of Y if some relationship has Y in its
// target_refs and X in its source_refs (or as the reasoning_ref). Relationship
// elements themselves are NOT counted as descendants.
int CountDescendants(const parser::AssuranceCase& ac, const std::string& id);

// How a remove operation should expand from the selected element.
//   NodeOnly            - just the element + its Group2 attachments
//                         (Context, Assumption, Justification). Structural
//                         children are reparented to the element's parent.
//   NodeAndDescendants  - the element and every descendant in its subtree.
enum class RemoveMode {
    NodeOnly,
    NodeAndDescendants,
};

// Compute the closed set of NODE ids that would be deleted by RemoveElement
// with the given mode. Pure: does not mutate the model. Used by the menu to
// label items with the count, and by the UI to highlight nodes pending
// confirmation. Relationship element ids are NOT included.
std::unordered_set<std::string> PlanRemoval(const parser::AssuranceCase& ac,
                                            const std::string& id,
                                            RemoveMode mode);

// Remove the element with id `id` from both the parser and sacm models, plus
// any relationship elements that become structurally empty as a result.
// Behavior depends on `mode` (see RemoveMode above). For NodeOnly, structural
// children of `id` are reparented to its structural parent before deletion.
// Returns true on success; on failure writes a human-readable reason into
// out_error and leaves the models unchanged.
bool RemoveElement(parser::AssuranceCase& ac,
                   sacm::AssuranceCasePackage* pkg,
                   const std::string& id,
                   RemoveMode mode,
                   std::string& out_error);

}  // namespace core
