#pragma once

#include "sacm/validation/diagnostic.h"

#include <vector>

namespace sacm::model {
class Document;
}

namespace sacm::validation {

// Cheap structural validation, run after every mutation (settled decision
// #16): unique IDs, resolvable references, legal reference target kinds.
std::vector<Diagnostic> validate_structure(const model::Document& document);

// Full conformance validation for the implemented slices, run on save or on
// demand: structural checks plus multiplicities, enumeration literals,
// citation/abstraction constraints, and relationship typing rules.
std::vector<Diagnostic> validate(const model::Document& document);

}  // namespace sacm::validation
