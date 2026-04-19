#pragma once

#include "gsn_model.h"
#include "core/assurance_tree.h"
#include <vector>

namespace ui {

// Pure layout engine: deterministic placement of nodes.
class LayoutEngine {
public:
    // Compute layout from a tree (new — spec-compliant)
    std::vector<LayoutNode> ComputeLayout(const core::AssuranceTree& tree);

    // Legacy: compute layout from flat element list (placeholder, deprecated)
    std::vector<LayoutNode> ComputeLayout(const std::vector<CanvasElement>& elements);
};

} // namespace ui
