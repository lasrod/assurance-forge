#include "gsn_canvas_renderer.h"
#include "gsn_layout.h"
#include "gsn_canvas.h" // for DrawGsnNode
#include <imgui.h>
#include <cmath>

namespace ui {

// Draw a solid (filled) arrowhead at 'tip' pointing in direction from 'from' to 'tip'
static void DrawSolidArrow(ImDrawList* dl, ImVec2 from, ImVec2 tip, ImU32 col, float size = 8.0f) {
    float dx = tip.x - from.x;
    float dy = tip.y - from.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    dx /= len; dy /= len;
    // Perpendicular
    float px = -dy, py = dx;
    ImVec2 p1(tip.x - dx * size + px * size * 0.5f, tip.y - dy * size + py * size * 0.5f);
    ImVec2 p2(tip.x - dx * size - px * size * 0.5f, tip.y - dy * size - py * size * 0.5f);
    dl->AddTriangleFilled(tip, p1, p2, col);
}

// Draw a hollow (outline) arrowhead at 'tip' pointing in direction from 'from' to 'tip'
static void DrawHollowArrow(ImDrawList* dl, ImVec2 from, ImVec2 tip, ImU32 col, float size = 8.0f, float thickness = 1.5f) {
    float dx = tip.x - from.x;
    float dy = tip.y - from.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    dx /= len; dy /= len;
    float px = -dy, py = dx;
    ImVec2 p1(tip.x - dx * size + px * size * 0.5f, tip.y - dy * size + py * size * 0.5f);
    ImVec2 p2(tip.x - dx * size - px * size * 0.5f, tip.y - dy * size - py * size * 0.5f);
    dl->AddTriangle(tip, p1, p2, col, thickness);
}

// Draw a dashed line between two points
static void DrawDashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness = 1.5f, float dash = 8.0f, float gap = 5.0f) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    float ux = dx / len, uy = dy / len;
    float t = 0.0f;
    while (t < len) {
        float seg = (t + dash < len) ? dash : (len - t);
        ImVec2 p1(a.x + ux * t, a.y + uy * t);
        ImVec2 p2(a.x + ux * (t + seg), a.y + uy * (t + seg));
        dl->AddLine(p1, p2, col, thickness);
        t += dash + gap;
    }
}

GsnCanvas::GsnCanvas() {
}

void GsnCanvas::SetTree(const core::AssuranceTree& tree) {
    LayoutEngine le;
    layout_nodes_ = le.ComputeLayout(tree);
}

void GsnCanvas::SetElements(const std::vector<CanvasElement>& elements) {
    elements_ = elements;
    LayoutEngine le;
    layout_nodes_ = le.ComputeLayout(elements_);
}

void GsnCanvas::Render() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetCursorScreenPos();

    // Compute content extents for scrollable area
    float max_x = 0.0f, max_y = 0.0f;
    for (const auto& n : layout_nodes_) {
        float right = n.position.x + n.size.x;
        float bottom = n.position.y + n.size.y;
        if (right > max_x) max_x = right;
        if (bottom > max_y) max_y = bottom;
    }
    // Add padding so nodes at the edge aren't clipped
    const float kPadding = 40.0f;
    max_x += kPadding;
    max_y += kPadding;

    // Declare content size so ImGui provides scrollbars
    ImGui::SetCursorPos(ImVec2(max_x, max_y));
    ImGui::Dummy(ImVec2(0, 0));
    // Reset cursor for drawing
    ImGui::SetCursorScreenPos(win_pos);

    // Draw links first
    for (const auto& n : layout_nodes_) {
        if (!n.parent_id.empty()) {
            auto it = std::find_if(layout_nodes_.begin(), layout_nodes_.end(),
                [&](const LayoutNode& p) { return p.id == n.parent_id; });
            if (it != layout_nodes_.end()) {
                ImVec2 parent_bottom = ImVec2(
                    win_pos.x + it->position.x + it->size.x * 0.5f,
                    win_pos.y + it->position.y + it->size.y);
                ImVec2 child_top = ImVec2(
                    win_pos.x + n.position.x + n.size.x * 0.5f,
                    win_pos.y + n.position.y);

                if (n.group == ElementGroup::Group2) {
                    // Group2: dashed line with hollow arrowhead
                    ImVec2 parent_side;
                    if (n.is_left_side) {
                        parent_side = ImVec2(
                            win_pos.x + it->position.x,
                            win_pos.y + it->position.y + it->size.y * 0.5f);
                    } else {
                        parent_side = ImVec2(
                            win_pos.x + it->position.x + it->size.x,
                            win_pos.y + it->position.y + it->size.y * 0.5f);
                    }
                    ImVec2 att_edge;
                    if (n.is_left_side) {
                        // Attachment is to the left — line arrives at its right edge
                        att_edge = ImVec2(
                            win_pos.x + n.position.x + n.size.x,
                            win_pos.y + n.position.y + n.size.y * 0.5f);
                    } else {
                        // Attachment is to the right — line arrives at its left edge
                        att_edge = ImVec2(
                            win_pos.x + n.position.x,
                            win_pos.y + n.position.y + n.size.y * 0.5f);
                    }
                    ImU32 col = IM_COL32(120,120,200,200);
                    DrawDashedLine(dl, parent_side, att_edge, col, 1.5f);
                    DrawHollowArrow(dl, parent_side, att_edge, col);
                } else {
                    // Group1: solid line with solid arrowhead pointing to child
                    ImU32 col = IM_COL32(180,180,180,255);
                    dl->AddLine(parent_bottom, child_top, col, 2.0f);
                    DrawSolidArrow(dl, parent_bottom, child_top, col);
                }
            }
        }
    }

    // Draw nodes using the same origin as lines
    for (const auto& n : layout_nodes_) {
        GsnNode gn;
        gn.id = n.id;
        switch (n.role) {
            case ElementRole::Claim:         gn.type = "Claim"; break;
            case ElementRole::Strategy:      gn.type = "Strategy"; break;
            case ElementRole::Solution:      gn.type = "Solution"; break;
            case ElementRole::Context:       gn.type = "Context"; break;
            case ElementRole::Assumption:    gn.type = "Assumption"; break;
            case ElementRole::Justification: gn.type = "Justification"; break;
            case ElementRole::Evidence:      gn.type = "Evidence"; break;
            default:                         gn.type = "Other"; break;
        }
        gn.position = n.position;
        gn.size = n.size;
        gn.label = n.label;
        DrawGsnNode(gn, win_pos);
    }
}

} // namespace ui
