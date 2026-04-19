#pragma once

#include <string>
#include <vector>
#include "imgui.h"

namespace ui {

enum class ElementRole {
    Claim,
    Context,
    Justification,
    Assumption,
    SubClaim,
    Solution,
    Evidence,
    Strategy,
    Other
};

enum class ElementGroup {
    Group1,  // Structural (below parent)
    Group2   // Contextual (side-attached)
};

struct CanvasElement {
    std::string id;
    ElementRole role = ElementRole::Other;
    std::string label;
    std::string parent_id; // empty if none
};

struct LayoutNode {
    std::string id;
    ElementRole role = ElementRole::Other;
    ElementGroup group = ElementGroup::Group1;
    std::string label;
    ImVec2 position;
    ImVec2 size;
    std::string parent_id;
    int side_stack_index = 0; // for Group2: 0-based index in stack on same side
    bool is_left_side = true; // for Group2: which side of parent
};

} // namespace ui
