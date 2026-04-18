#include "gsn_canvas_renderer.h"
#include "gsn_layout.h"
#include "gsn_canvas.h" // for DrawGsnNode
#include <imgui.h>

namespace ui {

GsnCanvas::GsnCanvas() {
}

void GsnCanvas::SetElements(const std::vector<CanvasElement>& elements) {
    elements_ = elements;
    // Compute layout immediately
    LayoutEngine le;
    layout_nodes_ = le.ComputeLayout(elements_);
}

void GsnCanvas::Render() {
    // Draw layout nodes and simple parent-child links
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetCursorScreenPos();

    // Draw links first
    for (const auto& n : layout_nodes_) {
        if (!n.parent_id.empty()) {
            // find parent
            auto it = std::find_if(layout_nodes_.begin(), layout_nodes_.end(), [&](const LayoutNode& p) { return p.id == n.parent_id; });
            if (it != layout_nodes_.end()) {
                ImVec2 parent_center = ImVec2(win_pos.x + it->position.x + it->size.x * 0.5f, win_pos.y + it->position.y + it->size.y);
                ImVec2 child_center = ImVec2(win_pos.x + n.position.x + n.size.x * 0.5f, win_pos.y + n.position.y);
                dl->AddLine(parent_center, child_center, IM_COL32(180,180,180,200), 2.0f);
            }
        }
    }

    // Draw nodes
    for (const auto& n : layout_nodes_) {
        GsnNode gn;
        gn.id = n.id;
        // map role to type string for color mapping
        switch (n.role) {
            case ElementRole::Claim: gn.type = "Claim"; break;
            case ElementRole::Evidence: gn.type = "Evidence"; break;
            default: gn.type = "Other"; break;
        }
        gn.position = n.position;
        gn.size = n.size;
        gn.label = n.label;
        DrawGsnNode(gn);
    }
}

} // namespace ui
