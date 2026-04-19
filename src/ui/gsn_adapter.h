#pragma once

#include "gsn_model.h"
#include "../parser/xml_parser.h"
#include <vector>

namespace ui {

// Convert parsed AssuranceCase into CanvasElement list. Parent relationships
// are left empty (parser currently doesn't expose links). This adapter
// normalizes types.
std::vector<CanvasElement> ConvertFromAssuranceCase(const parser::AssuranceCase& ac);

} // namespace ui
