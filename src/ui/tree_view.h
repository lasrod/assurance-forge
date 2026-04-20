#pragma once

#include "core/assurance_tree.h"

namespace ui {

// Render a tree-view panel for the safety case hierarchy.
// Expects to be called inside an ImGui::Begin/End block.
void ShowTreeViewPanel(const core::AssuranceTree* tree);

}  // namespace ui
