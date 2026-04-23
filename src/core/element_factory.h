#pragma once

#include <string>
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

// Remove the element with id `id` from both the parser and sacm models, plus
// any relationship elements that reference removed ids.
//   - cascade=false: fails (returns false, sets out_error) if the element has
//     descendants. Use for the "leaf-only" remove path.
//   - cascade=true:  also removes every descendant reachable from `id`.
// Returns true on success.
bool RemoveElement(parser::AssuranceCase& ac,
                   sacm::AssuranceCasePackage* pkg,
                   const std::string& id,
                   bool cascade,
                   std::string& out_error);

}  // namespace core
