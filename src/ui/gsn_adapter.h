#pragma once

#include "gsn_model.h"
#include "../parser/xml_parser.h"
#include "../core/assurance_tree.h"
#include <vector>

namespace ui {

// Convert parsed AssuranceCase into CanvasElement list (legacy).
std::vector<CanvasElement> ConvertFromAssuranceCase(const parser::AssuranceCase& ac);

// Build an AssuranceTree from a parsed assurance case.
core::AssuranceTree BuildAssuranceTree(const parser::AssuranceCase& ac);

} // namespace ui
