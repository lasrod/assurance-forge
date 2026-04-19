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

struct CanvasElement {
    std::string id;
    ElementRole role = ElementRole::Other;
    std::string label;
    std::string parent_id; // empty if none
};

struct LayoutNode {
    std::string id;
    ElementRole role = ElementRole::Other;
    std::string label;
    ImVec2 position;
    ImVec2 size;
    std::string parent_id;
};

} // namespace ui
