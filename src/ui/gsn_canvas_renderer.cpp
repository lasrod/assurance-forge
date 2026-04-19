#include "gsn_canvas_renderer.h"
#include "gsn_layout.h"
#include "gsn_canvas.h" // for DrawGsnNode
#include <imgui.h>
#include <cmath>

namespace ui {

// Draw a solid (filled) arrowhead at 'tip' pointing in the given direction (dx,dy)
static void DrawSolidArrowDir(ImDrawList* dl, ImVec2 tip, float dx, float dy, ImU32 col, float size = 8.0f) {
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    dx /= len; dy /= len;
    float px = -dy, py = dx;
    ImVec2 p1(tip.x - dx * size + px * size * 0.5f, tip.y - dy * size + py * size * 0.5f);
    ImVec2 p2(tip.x - dx * size - px * size * 0.5f, tip.y - dy * size - py * size * 0.5f);
    dl->AddTriangleFilled(tip, p1, p2, col);
}

// Draw a hollow (outline) arrowhead at 'tip' pointing in the given direction (dx,dy)
static void DrawHollowArrowDir(ImDrawList* dl, ImVec2 tip, float dx, float dy, ImU32 col, float size = 8.0f, float thickness = 1.5f) {
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    dx /= len; dy /= len;
    float px = -dy, py = dx;
    ImVec2 p1(tip.x - dx * size + px * size * 0.5f, tip.y - dy * size + py * size * 0.5f);
    ImVec2 p2(tip.x - dx * size - px * size * 0.5f, tip.y - dy * size - py * size * 0.5f);
    dl->AddTriangle(tip, p1, p2, col, thickness);
}

// Evaluate a cubic Bezier at parameter t
static ImVec2 BezierPoint(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
    float u = 1.0f - t;
    float uu = u * u, uuu = uu * u;
    float tt = t * t, ttt = tt * t;
    return ImVec2(
        uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x,
        uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y);
}

// Tangent of a cubic Bezier at parameter t (derivative)
static ImVec2 BezierTangent(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
    float u = 1.0f - t;
    return ImVec2(
        3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x),
        3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y));
}

// Draw a solid cubic Bezier curve
static void DrawBezierSolid(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, ImU32 col, float thickness = 2.0f) {
    dl->AddBezierCubic(p0, p1, p2, p3, col, thickness);
}

// Draw a dashed cubic Bezier curve by sampling it into segments
static void DrawBezierDashed(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, ImU32 col, float thickness = 1.5f, float dash = 8.0f, float gap = 5.0f) {
    const int n_samples = 64;
    // Build polyline with arc-length parameterization
    ImVec2 pts[n_samples + 1];
    float lengths[n_samples + 1];
    pts[0] = p0;
    lengths[0] = 0.0f;
    for (int i = 1; i <= n_samples; ++i) {
        float t = (float)i / (float)n_samples;
        pts[i] = BezierPoint(p0, p1, p2, p3, t);
        float dx = pts[i].x - pts[i-1].x;
        float dy = pts[i].y - pts[i-1].y;
        lengths[i] = lengths[i-1] + sqrtf(dx * dx + dy * dy);
    }
    float total_len = lengths[n_samples];
    if (total_len < 1.0f) return;

    // Walk along the curve drawing dash segments
    float arc = 0.0f;
    bool drawing = true;
    int seg = 0;
    while (arc < total_len && seg < n_samples) {
        float seg_end = arc + (drawing ? dash : gap);
        if (seg_end > total_len) seg_end = total_len;

        if (drawing) {
            // Find start and end sample indices
            ImVec2 start_pt = pts[0];
            ImVec2 end_pt = pts[0];
            // Interpolate start
            for (int i = 1; i <= n_samples; ++i) {
                if (lengths[i] >= arc) {
                    float frac = (lengths[i-1] == lengths[i]) ? 0.0f : (arc - lengths[i-1]) / (lengths[i] - lengths[i-1]);
                    start_pt = ImVec2(pts[i-1].x + frac * (pts[i].x - pts[i-1].x),
                                      pts[i-1].y + frac * (pts[i].y - pts[i-1].y));
                    break;
                }
            }
            for (int i = 1; i <= n_samples; ++i) {
                if (lengths[i] >= seg_end) {
                    float frac = (lengths[i-1] == lengths[i]) ? 0.0f : (seg_end - lengths[i-1]) / (lengths[i] - lengths[i-1]);
                    end_pt = ImVec2(pts[i-1].x + frac * (pts[i].x - pts[i-1].x),
                                    pts[i-1].y + frac * (pts[i].y - pts[i-1].y));
                    break;
                }
            }
            dl->AddLine(start_pt, end_pt, col, thickness);
        }
        arc = seg_end;
        drawing = !drawing;
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
                    // Group2: dashed Bezier with hollow arrowhead
                    // Start horizontally from parent side, end horizontally into attachment edge
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
                        att_edge = ImVec2(
                            win_pos.x + n.position.x + n.size.x,
                            win_pos.y + n.position.y + n.size.y * 0.5f);
                    } else {
                        att_edge = ImVec2(
                            win_pos.x + n.position.x,
                            win_pos.y + n.position.y + n.size.y * 0.5f);
                    }
                    // Short straight stubs at each end, then Bezier in between
                    float sign = n.is_left_side ? -1.0f : 1.0f;
                    const float stub = 12.0f;
                    ImVec2 stub_start(parent_side.x + sign * stub, parent_side.y);
                    ImVec2 stub_end(att_edge.x - sign * stub, att_edge.y);

                    float hdist = fabsf(stub_end.x - stub_start.x) * 0.5f;
                    ImVec2 cp1(stub_start.x + sign * hdist, stub_start.y);
                    ImVec2 cp2(stub_end.x   - sign * hdist, stub_end.y);

                    ImU32 col = IM_COL32(120,120,200,200);
                    DrawBezierDashed(dl, parent_side, parent_side, parent_side, stub_start, col, 1.5f); // straight stub
                    DrawBezierDashed(dl, stub_start, cp1, cp2, stub_end, col, 1.5f);
                    DrawBezierDashed(dl, stub_end, stub_end, stub_end, att_edge, col, 1.5f); // straight stub
                    // Arrow points into attachment node; tip at edge, base extends outward
                    DrawHollowArrowDir(dl, att_edge, sign, 0.0f, col);
                } else {
                    // Group1: solid Bezier with solid arrowhead
                    // Short straight stubs at each end, then Bezier in between
                    const float stub = 12.0f;
                    ImVec2 stub_start(parent_bottom.x, parent_bottom.y + stub);
                    ImVec2 stub_end(child_top.x, child_top.y - stub);

                    float vdist = fabsf(stub_end.y - stub_start.y) * 0.4f;
                    ImVec2 cp1(stub_start.x, stub_start.y + vdist);
                    ImVec2 cp2(stub_end.x,   stub_end.y   - vdist);

                    ImU32 col = IM_COL32(180,180,180,255);
                    dl->AddLine(parent_bottom, stub_start, col, 2.0f);   // straight stub
                    DrawBezierSolid(dl, stub_start, cp1, cp2, stub_end, col, 2.0f);
                    dl->AddLine(stub_end, child_top, col, 2.0f);         // straight stub
                    // Arrow points straight down
                    DrawSolidArrowDir(dl, child_top, 0.0f, 1.0f, col);
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
