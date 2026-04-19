#pragma once

#include "gsn_model.h"
#include "core/assurance_tree.h"
#include <vector>

namespace ui {

class GsnCanvas {
public:
    GsnCanvas();
    // Set elements from tree (new — spec-compliant layout)
    void SetTree(const core::AssuranceTree& tree);
    // Set elements (legacy flat list)
    void SetElements(const std::vector<CanvasElement>& elements);
    // Render into the current ImGui window/child
    void Render();

private:
    std::vector<CanvasElement> elements_;
    std::vector<LayoutNode> layout_nodes_;
};

} // namespace ui
