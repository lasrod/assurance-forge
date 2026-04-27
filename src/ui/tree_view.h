#pragma once

#include "core/assurance_tree.h"
#include "parser/xml_parser.h"
#include "ui/element_context_menu.h"
#include "ui/ui_state.h"

namespace ui {

// Render a tree-view panel for the safety case hierarchy.
// Expects to be called inside an ImGui::Begin/End block.
void ShowTreeViewPanel(const core::AssuranceTree* tree,
                       const parser::AssuranceCase* active_case,
                       UiState& state,
                       const ElementContextActions& actions);

}  // namespace ui
