#pragma once

#include "core/assurance_tree.h"

namespace ui {

// Render a tree-view panel for the safety case hierarchy.
// Expects to be called inside an ImGui::Begin/End block.
void ShowTreeViewPanel(const core::AssuranceTree* tree);

// Render the shared "Add" submenu (used by tree and canvas context menus).
// Must be called inside an active popup.
void RenderAddElementMenu();

// Render the shared "Remove" submenu (used by tree and canvas context menus).
// The submenu offers two modes; each item is labeled with the count of
// elements that would be removed. Must be called inside an active popup.
void RenderRemoveSubmenu();

}  // namespace ui
