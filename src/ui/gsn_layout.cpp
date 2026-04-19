#include "gsn_layout.h"
#include <algorithm>

namespace ui {

std::vector<LayoutNode> LayoutEngine::ComputeLayout(const std::vector<CanvasElement>& elements) {
    std::vector<LayoutNode> nodes;

    // Basic deterministic layout:
    // - Collect claims (roots) and place them in a top row in input order
    // - Other elements without parent are placed in rows below by role

    const ImVec2 node_size = ImVec2(180.0f, 80.0f);
    const float h_spacing = 40.0f;
    const float v_spacing = 80.0f;
    const float left_margin = 20.0f;
    const float top_margin = 20.0f;

    // Partition elements by role
    std::vector<CanvasElement> claims;
    std::vector<CanvasElement> others;
    for (const auto& e : elements) {
        if (e.role == ElementRole::Claim) claims.push_back(e);
        else others.push_back(e);
    }

    // Place claims
    for (size_t i = 0; i < claims.size(); ++i) {
        LayoutNode n;
        n.id = claims[i].id;
        n.role = claims[i].role;
        n.label = claims[i].label;
        n.size = node_size;
        n.position = ImVec2(left_margin + (float)i * (node_size.x + h_spacing), top_margin);
        n.parent_id = claims[i].parent_id;
        nodes.push_back(n);
    }

    // Place other nodes in rows below, grouped by role deterministically
    auto place_row = [&](ElementRole role, size_t row_index, const std::vector<CanvasElement>& pool) {
        std::vector<CanvasElement> row_elems;
        for (const auto& e : pool) if (e.role == role) row_elems.push_back(e);
        for (size_t i = 0; i < row_elems.size(); ++i) {
            LayoutNode n;
            n.id = row_elems[i].id;
            n.role = row_elems[i].role;
            n.label = row_elems[i].label;
            n.size = node_size;
            n.position = ImVec2(left_margin + (float)i * (node_size.x + h_spacing), top_margin + (float)(row_index) * (node_size.y + v_spacing));
            n.parent_id = row_elems[i].parent_id;
            nodes.push_back(n);
        }
    };

    // Deterministic order of role rows (below claims)
    std::vector<ElementRole> below_order = { ElementRole::Strategy, ElementRole::SubClaim, ElementRole::Solution, ElementRole::Evidence, ElementRole::Context, ElementRole::Justification, ElementRole::Assumption, ElementRole::Other };
    for (size_t r = 0; r < below_order.size(); ++r) {
        place_row(below_order[r], 1 + r, others);
    }

    return nodes;
}

} // namespace ui
