#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/gsn/gsn_layout.h"
#include "ui/gsn/gsn_canvas.h" // for DrawGsnNode
#include "ui/theme.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace ui::gsn {

// ===== Edge rendering constants =====
static constexpr float kArrowSize           = 9.0f;   // arrowhead triangle side length
static constexpr float kArrowOutlineWidth   = 1.5f;   // hollow arrowhead outline thickness
static constexpr float kSolidEdgeWidth      = 2.2f;   // Group1 solid line thickness
static constexpr float kDashedEdgeWidth     = 1.8f;   // Group2 dashed line thickness
static constexpr float kDashLength          = 6.0f;   // dash on-length for dashed lines
static constexpr float kDashGap             = 4.0f;   // dash off-length for dashed lines
static constexpr float kStubLength          = 12.0f;  // straight segment at each end of a Bezier curve
static constexpr float kVerticalControlPct  = 0.4f;   // Bezier control point distance (fraction of vertical span)
static constexpr float kScrollPadding       = 40.0f;  // extra padding beyond outermost node for scrolling
static constexpr int   kBezierSamples       = 64;     // arc-length sample count for dashed Bezier rendering

// Edge colors are sourced from the theme on every call so they update if the
// theme is ever swapped at runtime.
static ImU32 Group1EdgeColor() { return GetTheme().edge_group1; }
static ImU32 Group2EdgeColor() { return GetTheme().edge_group2; }

// ===== Zoom constants =====
static constexpr float kZoomMin      = 0.25f;  // minimum zoom level (25%)
static constexpr float kZoomMax      = 3.0f;   // maximum zoom level (300%)
static constexpr float kZoomStep     = 0.1f;   // zoom increment per step (10%)

// ===== Arrowhead helpers =====

// Compute the two base corners of an arrowhead triangle given its tip,
// a unit direction vector, and side length.
static void ComputeArrowBasePoints(ImVec2 tip, float dir_x, float dir_y, float size,
                                   ImVec2& out_left, ImVec2& out_right) {
    // Perpendicular to the direction vector
    float perp_x = -dir_y;
    float perp_y =  dir_x;
    float half = size * 0.5f;
    out_left  = ImVec2(tip.x - dir_x * size + perp_x * half,
                       tip.y - dir_y * size + perp_y * half);
    out_right = ImVec2(tip.x - dir_x * size - perp_x * half,
                       tip.y - dir_y * size - perp_y * half);
}

// Draw a solid (filled) arrowhead at 'tip' pointing in direction (dir_x, dir_y).
static void DrawSolidArrow(ImDrawList* draw_list, ImVec2 tip, float dir_x, float dir_y,
                           ImU32 color, float size = kArrowSize) {
    float length = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (length < 1.0f) return;
    dir_x /= length;
    dir_y /= length;
    ImVec2 base_left, base_right;
    ComputeArrowBasePoints(tip, dir_x, dir_y, size, base_left, base_right);
    draw_list->AddTriangleFilled(tip, base_left, base_right, color);
}

// Draw a hollow (outline-only) arrowhead at 'tip' pointing in direction (dir_x, dir_y).
static void DrawHollowArrow(ImDrawList* draw_list, ImVec2 tip, float dir_x, float dir_y,
                            ImU32 color, float size = kArrowSize, float thickness = kArrowOutlineWidth) {
    float length = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (length < 1.0f) return;
    dir_x /= length;
    dir_y /= length;
    ImVec2 base_left, base_right;
    ComputeArrowBasePoints(tip, dir_x, dir_y, size, base_left, base_right);
    draw_list->AddTriangle(tip, base_left, base_right, color, thickness);
}

// ===== Bezier curve helpers =====

// Evaluate a cubic Bezier at parameter t âˆˆ [0,1].
static ImVec2 EvalBezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
    float u = 1.0f - t;
    float uu = u * u, uuu = uu * u;
    float tt = t * t, ttt = tt * t;
    return ImVec2(
        uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x,
        uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y);
}

// Draw a solid cubic Bezier curve.
static void DrawSolidBezier(ImDrawList* draw_list, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3,
                            ImU32 color, float thickness = kSolidEdgeWidth) {
    draw_list->AddBezierCubic(p0, p1, p2, p3, color, thickness);
}

// Interpolate a point along a sampled polyline at the given arc-length distance.
static ImVec2 InterpolateAtArcLength(const ImVec2* samples, const float* cumulative_lengths,
                                     int sample_count, float target_arc) {
    for (int i = 1; i <= sample_count; ++i) {
        if (cumulative_lengths[i] >= target_arc) {
            float segment_len = cumulative_lengths[i] - cumulative_lengths[i - 1];
            float fraction = (segment_len < 1e-6f) ? 0.0f
                           : (target_arc - cumulative_lengths[i - 1]) / segment_len;
            return ImVec2(samples[i - 1].x + fraction * (samples[i].x - samples[i - 1].x),
                          samples[i - 1].y + fraction * (samples[i].y - samples[i - 1].y));
        }
    }
    return samples[sample_count];
}

// Draw a dashed cubic Bezier curve using arc-length parameterized sampling.
static void DrawDashedBezier(ImDrawList* draw_list, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3,
                             ImU32 color, float thickness = kDashedEdgeWidth,
                             float dash_on = kDashLength, float dash_off = kDashGap) {
    // Sample the curve into a polyline and compute cumulative arc lengths
    ImVec2 samples[kBezierSamples + 1];
    float  cumulative_lengths[kBezierSamples + 1];
    samples[0] = p0;
    cumulative_lengths[0] = 0.0f;
    for (int i = 1; i <= kBezierSamples; ++i) {
        float t = (float)i / (float)kBezierSamples;
        samples[i] = EvalBezier(p0, p1, p2, p3, t);
        float dx = samples[i].x - samples[i - 1].x;
        float dy = samples[i].y - samples[i - 1].y;
        cumulative_lengths[i] = cumulative_lengths[i - 1] + sqrtf(dx * dx + dy * dy);
    }
    float total_arc = cumulative_lengths[kBezierSamples];
    if (total_arc < 1.0f) return;

    // Walk along the curve alternating between drawing and skipping
    float arc_pos = 0.0f;
    bool is_visible = true;
    while (arc_pos < total_arc) {
        float segment_end = arc_pos + (is_visible ? dash_on : dash_off);
        if (segment_end > total_arc) segment_end = total_arc;

        if (is_visible) {
            ImVec2 dash_start = InterpolateAtArcLength(samples, cumulative_lengths, kBezierSamples, arc_pos);
            ImVec2 dash_end   = InterpolateAtArcLength(samples, cumulative_lengths, kBezierSamples, segment_end);
            draw_list->AddLine(dash_start, dash_end, color, thickness);
        }
        arc_pos = segment_end;
        is_visible = !is_visible;
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

void GsnCanvas::ZoomIn() {
    zoom_level_ = std::min(zoom_level_ + kZoomStep, kZoomMax);
}

void GsnCanvas::ZoomOut() {
    zoom_level_ = std::max(zoom_level_ - kZoomStep, kZoomMin);
}

void GsnCanvas::ResetZoom() {
    zoom_level_ = 1.0f;
}

void GsnCanvas::ZoomAtPoint(float new_zoom, ImVec2 focus_content) {
    float old_zoom = zoom_level_;
    zoom_level_ = std::max(kZoomMin, std::min(new_zoom, kZoomMax));
    // Adjust view_offset_ so the content point under the mouse stays fixed on screen.
    // screen_pos = canvas_origin + content * zoom - view_offset
    // We want the same screen_pos before and after:
    //   view_offset_new = view_offset_old + focus * (new_zoom - old_zoom)
    view_offset_.x += focus_content.x * (zoom_level_ - old_zoom);
    view_offset_.y += focus_content.y * (zoom_level_ - old_zoom);
}

void GsnCanvas::Pan(float dx, float dy) {
    view_offset_.x += dx;
    view_offset_.y += dy;
}

void GsnCanvas::GetContentBounds(ImVec2& out_min, ImVec2& out_max) const {
    if (layout_nodes_.empty()) {
        out_min = ImVec2(0, 0);
        out_max = ImVec2(0, 0);
        return;
    }
    float min_x = FLT_MAX, min_y = FLT_MAX;
    float max_x = -FLT_MAX, max_y = -FLT_MAX;
    for (const auto& node : layout_nodes_) {
        if (node.position.x < min_x) min_x = node.position.x;
        if (node.position.y < min_y) min_y = node.position.y;
        float r = node.position.x + node.size.x;
        float b = node.position.y + node.size.y;
        if (r > max_x) max_x = r;
        if (b > max_y) max_y = b;
    }
    // Add some padding around the content
    float pad = 40.0f;
    out_min = ImVec2(min_x - pad, min_y - pad);
    out_max = ImVec2(max_x + pad, max_y + pad);
}

// ===== Edge drawing helpers =====

// Compute the screen-space connection points for a parentâ†’child edge.
// Group1 edges go from parent's bottom center to child's top center.
static void ComputeGroup1Endpoints(const LayoutNode& parent, const LayoutNode& child,
                                   ImVec2 origin, float zoom,
                                   ImVec2& out_start, ImVec2& out_end) {
    out_start = ImVec2(origin.x + (parent.position.x + parent.size.x * 0.5f) * zoom,
                       origin.y + (parent.position.y + parent.size.y) * zoom);
    out_end   = ImVec2(origin.x + (child.position.x + child.size.x * 0.5f) * zoom,
                       origin.y + child.position.y * zoom);
}

// Draw a Group1 (structural) edge: straight stubs â†’ solid Bezier â†’ solid arrowhead.
static void DrawGroup1Edge(ImDrawList* draw_list, ImVec2 parent_bottom, ImVec2 child_top, float zoom) {
    float scaled_stub = kStubLength * zoom;
    float scaled_edge_width = kSolidEdgeWidth * zoom;
    float scaled_arrow = kArrowSize * zoom;

    ImVec2 stub_start(parent_bottom.x, parent_bottom.y + scaled_stub);
    ImVec2 stub_end(child_top.x, child_top.y - scaled_stub);

    float vertical_span = fabsf(stub_end.y - stub_start.y);
    ImVec2 ctrl_1(stub_start.x, stub_start.y + vertical_span * kVerticalControlPct);
    ImVec2 ctrl_2(stub_end.x,   stub_end.y   - vertical_span * kVerticalControlPct);

    ImU32 col = Group1EdgeColor();
    draw_list->AddLine(parent_bottom, stub_start, col, scaled_edge_width);
    DrawSolidBezier(draw_list, stub_start, ctrl_1, ctrl_2, stub_end, col, scaled_edge_width);
    draw_list->AddLine(stub_end, child_top, col, scaled_edge_width);
    DrawSolidArrow(draw_list, child_top, 0.0f, 1.0f, col, scaled_arrow);
}

// Compute screen-space endpoints for a Group2 (side-attached) edge.
// Parent side â†’ attachment nearest edge, depending on which side.
static void ComputeGroup2Endpoints(const LayoutNode& parent, const LayoutNode& attachment,
                                   ImVec2 origin, float zoom,
                                   ImVec2& out_parent_side, ImVec2& out_attachment_edge) {
    if (attachment.is_left_side) {
        out_parent_side = ImVec2(origin.x + parent.position.x * zoom,
                                 origin.y + (parent.position.y + parent.size.y * 0.5f) * zoom);
        out_attachment_edge = ImVec2(origin.x + (attachment.position.x + attachment.size.x) * zoom,
                                     origin.y + (attachment.position.y + attachment.size.y * 0.5f) * zoom);
    } else {
        out_parent_side = ImVec2(origin.x + (parent.position.x + parent.size.x) * zoom,
                                 origin.y + (parent.position.y + parent.size.y * 0.5f) * zoom);
        out_attachment_edge = ImVec2(origin.x + attachment.position.x * zoom,
                                     origin.y + (attachment.position.y + attachment.size.y * 0.5f) * zoom);
    }
}

// Draw a Group2 (contextual) edge: dashed stubs â†’ dashed Bezier â†’ hollow arrowhead.
static void DrawGroup2Edge(ImDrawList* draw_list, ImVec2 parent_side, ImVec2 attachment_edge,
                           bool is_left_side, float zoom) {
    // Sign encodes horizontal direction: -1 toward left, +1 toward right
    float horizontal_sign = is_left_side ? -1.0f : 1.0f;
    float scaled_stub = kStubLength * zoom;
    float scaled_edge_width = kDashedEdgeWidth * zoom;

    ImVec2 stub_start(parent_side.x + horizontal_sign * scaled_stub, parent_side.y);
    ImVec2 stub_end(attachment_edge.x - horizontal_sign * scaled_stub, attachment_edge.y);

    float horizontal_span = fabsf(stub_end.x - stub_start.x) * 0.5f;
    ImVec2 ctrl_1(stub_start.x + horizontal_sign * horizontal_span, stub_start.y);
    ImVec2 ctrl_2(stub_end.x   - horizontal_sign * horizontal_span, stub_end.y);

    // Straight stub from parent â†’ dashed Bezier â†’ straight stub into attachment
    ImU32 col = Group2EdgeColor();
    DrawDashedBezier(draw_list, parent_side, parent_side, parent_side, stub_start, col, scaled_edge_width);
    DrawDashedBezier(draw_list, stub_start, ctrl_1, ctrl_2, stub_end, col, scaled_edge_width);
    DrawDashedBezier(draw_list, stub_end, stub_end, stub_end, attachment_edge, col, scaled_edge_width);
    // Arrow points into the attachment node
    DrawHollowArrow(draw_list, attachment_edge, horizontal_sign, 0.0f, col, kArrowSize * zoom);
}

// ===== Main rendering =====

void GsnCanvas::Render(UiState& ui_state,
                       const parser::AssuranceCase* active_case,
                       const ElementContextActions& actions) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    float zoom = zoom_level_;

    // Apply our own view offset to the drawing origin (replaces ImGui scroll)
    ImVec2 origin(canvas_pos.x - view_offset_.x, canvas_pos.y - view_offset_.y);

    // Build a lookup map for finding parent nodes by ID
    std::unordered_map<std::string, const LayoutNode*> node_by_id;
    node_by_id.reserve(layout_nodes_.size());
    for (const auto& node : layout_nodes_) {
        node_by_id[node.id] = &node;
    }

    // Draw edges first (beneath nodes)
    for (const auto& child_node : layout_nodes_) {
        if (child_node.parent_id.empty()) continue;

        auto parent_it = node_by_id.find(child_node.parent_id);
        if (parent_it == node_by_id.end()) continue;
        const LayoutNode& parent_node = *parent_it->second;

        if (child_node.group == ElementGroup::Group2) {
            ImVec2 parent_side, attachment_edge;
            ComputeGroup2Endpoints(parent_node, child_node, origin, zoom,
                                   parent_side, attachment_edge);
            DrawGroup2Edge(draw_list, parent_side, attachment_edge, child_node.is_left_side, zoom);
        } else {
            ImVec2 parent_bottom, child_top;
            ComputeGroup1Endpoints(parent_node, child_node, origin, zoom,
                                   parent_bottom, child_top);
            DrawGroup1Edge(draw_list, parent_bottom, child_top, zoom);
        }
    }

    // Draw nodes on top of edges
    for (const auto& node : layout_nodes_) {
        GsnNode gsn_node;
        gsn_node.id = node.id;
        switch (node.role) {
            case ElementRole::Claim:         gsn_node.type = "Claim"; break;
            case ElementRole::Strategy:      gsn_node.type = "Strategy"; break;
            case ElementRole::Solution:      gsn_node.type = "Solution"; break;
            case ElementRole::Context:       gsn_node.type = "Context"; break;
            case ElementRole::Assumption:    gsn_node.type = "Assumption"; break;
            case ElementRole::Justification: gsn_node.type = "Justification"; break;
            case ElementRole::Evidence:      gsn_node.type = "Evidence"; break;
            default:                         gsn_node.type = "Other"; break;
        }
        gsn_node.position = node.position;
        gsn_node.size = node.size;
        gsn_node.label = node.label;
        gsn_node.label_secondary = node.label_secondary;
        gsn_node.undeveloped = node.undeveloped;
        DrawGsnNode(gsn_node, origin, ui_state, active_case, actions, zoom);
    }
}

bool GsnCanvas::CenterOnNode(const std::string& node_id, ImVec2 viewport_size) {
    for (const auto& node : layout_nodes_) {
        if (node.id == node_id) {
            // Center of the node in layout-space (unzoomed)
            float cx = node.position.x + node.size.x * 0.5f;
            float cy = node.position.y + node.size.y * 0.5f;
            // Set view_offset so the node center maps to viewport center
            view_offset_.x = cx * zoom_level_ - viewport_size.x * 0.5f;
            view_offset_.y = cy * zoom_level_ - viewport_size.y * 0.5f;
            return true;
        }
    }
    return false;
}

bool GsnCanvas::CenterOnIds(const std::unordered_set<std::string>& ids,
                            ImVec2 viewport_size) {
    if (ids.empty()) return false;

    bool found_any = false;
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    for (const auto& node : layout_nodes_) {
        if (!ids.count(node.id)) continue;
        const float nx0 = node.position.x;
        const float ny0 = node.position.y;
        const float nx1 = nx0 + node.size.x;
        const float ny1 = ny0 + node.size.y;
        if (!found_any) {
            min_x = nx0; min_y = ny0; max_x = nx1; max_y = ny1;
            found_any = true;
        } else {
            if (nx0 < min_x) min_x = nx0;
            if (ny0 < min_y) min_y = ny0;
            if (nx1 > max_x) max_x = nx1;
            if (ny1 > max_y) max_y = ny1;
        }
    }
    if (!found_any) return false;

    // Pad the AABB so the marked nodes don't sit flush with the viewport edge.
    const float padding = 80.0f;  // layout-space pixels
    const float aabb_w = std::max(1.0f, (max_x - min_x) + padding * 2.0f);
    const float aabb_h = std::max(1.0f, (max_y - min_y) + padding * 2.0f);

    // Compute zoom that fits AABB in viewport, then clamp.
    const float zoom_x = viewport_size.x / aabb_w;
    const float zoom_y = viewport_size.y / aabb_h;
    float new_zoom = std::min(zoom_x, zoom_y);
    // Clamp to a sensible range; do not zoom further IN than 1.0 (no need).
    if (new_zoom > 1.0f) new_zoom = 1.0f;
    if (new_zoom < 0.1f) new_zoom = 0.1f;
    zoom_level_ = new_zoom;

    const float cx = (min_x + max_x) * 0.5f;
    const float cy = (min_y + max_y) * 0.5f;
    view_offset_.x = cx * zoom_level_ - viewport_size.x * 0.5f;
    view_offset_.y = cy * zoom_level_ - viewport_size.y * 0.5f;
    return true;
}

} // namespace ui::gsn
