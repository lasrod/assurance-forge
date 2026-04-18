#pragma once

#include "gsn_model.h"
#include <vector>

namespace ui {

class GsnCanvas {
public:
    GsnCanvas();
    // Set elements (from parser adapter)
    void SetElements(const std::vector<CanvasElement>& elements);
    // Render into the current ImGui window/child
    void Render();

private:
    std::vector<CanvasElement> elements_;
    std::vector<LayoutNode> layout_nodes_;
};

} // namespace ui
