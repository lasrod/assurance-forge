#pragma once

#include "core/assurance_tree.h"
#include "core/tree_editing.h"
#include "core/sacm_model.h"
#include "ui/element_context_menu.h"
#include "ui/ui_state.h"

#include <functional>
#include <string>

namespace ui {

struct TreeEditActions {
    std::function<core::TreeDropValidationResult(const std::string&, const std::string&, core::TreeDropMode)>
        validate_drop;
    std::function<bool(const std::string&, const std::string&, core::TreeDropMode)> perform_drop;

    bool enabled() const {
        return static_cast<bool>(validate_drop) && static_cast<bool>(perform_drop);
    }
};

// The single line the Argument Navigator shows for a node.
//
// A tree node's label is "ID: name" with the element's text on the line below,
// and the navigator has room for one line. Nothing in Assurance Forge fills
// `name` when it creates an element -- not the New Goal menu item, not an agent
// staging a CreateClaim -- so taking the first line alone produced a column of
// "G2:", "G3:", "S1:" identifying nothing, beside a canvas drawing the text
// perfectly well. Exposed so that is a tested rule rather than a detail of the
// row-drawing code.
std::string TreeNodeDisplayName(const core::TreeNode& node, const UiState& state);

// Render a tree-view panel for the safety case hierarchy.
// Expects to be called inside an ImGui::Begin/End block.
void ShowTreeViewPanel(const core::AssuranceTree* tree,
                       const parser::AssuranceCase* active_case,
                       UiState& state,
                       const ElementContextActions& actions,
                       const TreeEditActions* tree_edit_actions = nullptr);

} // namespace ui
