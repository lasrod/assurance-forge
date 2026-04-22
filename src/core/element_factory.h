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

}  // namespace core
