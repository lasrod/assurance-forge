#pragma once

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace ui::panels {

// Render the element properties panel with editable fields.
// Returns true if any field was modified (caller should rebuild tree).
bool ShowElementPanel(parser::AssuranceCase* ac, sacm::AssuranceCasePackage* sacm_pkg);

}  // namespace ui::panels
