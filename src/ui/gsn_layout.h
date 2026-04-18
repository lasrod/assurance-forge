#pragma once

#include "gsn_model.h"
#include <vector>

namespace ui {

// Pure layout engine: deterministic placement of nodes.
class LayoutEngine {
public:
    // Compute layout for provided canvas elements. Deterministic.
    std::vector<LayoutNode> ComputeLayout(const std::vector<CanvasElement>& elements);
};

} // namespace ui
