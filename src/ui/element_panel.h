#pragma once

#include "parser/xml_parser.h"

namespace ui {

// Render the element properties panel showing details of the selected element.
// Expects to be called inside an ImGui::Begin/End block.
void ShowElementPanel(const parser::AssuranceCase* ac);

}  // namespace ui
