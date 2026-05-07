#pragma once

#include "core/assurance_tree.h"
#include "core/tree_editing.h"
#include "parser/xml_parser.h"
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

// Render a tree-view panel for the safety case hierarchy.
// Expects to be called inside an ImGui::Begin/End block.
void ShowTreeViewPanel(const core::AssuranceTree* tree,
                       const parser::AssuranceCase* active_case,
                       UiState& state,
                       const ElementContextActions& actions,
                       const TreeEditActions* tree_edit_actions = nullptr);

} // namespace ui
