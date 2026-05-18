#pragma once

#include "core/assurance_tree.h"
#include "core/sacm_model.h"
#include "ui/gsn/gsn_model.h"

#include <vector>

namespace ui::gsn {

// Convert parsed AssuranceCase into CanvasElement list (legacy).
std::vector<CanvasElement> ConvertFromAssuranceCase(const parser::AssuranceCase& ac);

// Build an AssuranceTree from a parsed assurance case.
core::AssuranceTree BuildAssuranceTree(const parser::AssuranceCase& ac);

} // namespace ui::gsn
