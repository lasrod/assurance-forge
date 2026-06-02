#include "ui/gsn/gsn_edge_renderer.h"

#include "ui/gsn/gsn_dpi.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

namespace ui::gsn {

namespace {

// ===== Edge rendering constants =====
constexpr float kArrowSize = 9.0f;         // arrowhead triangle side length
constexpr float kArrowOutlineWidth = 1.5f; // hollow arrowhead outline thickness
constexpr float kSolidEdgeWidth = 2.2f;    // Group1 solid line thickness
constexpr float kDashedEdgeWidth = 1.8f;   // Group2 dashed line thickness
constexpr float kDashLength = 6.0f;        // dash on-length for dashed lines
constexpr float kDashGap = 4.0f;           // dash off-length for dashed lines
// kStubLength, kVerticalControlPct, kBezierSamples are declared in the header so
// hit-testing code in the renderer can share them.

// Edge colors are sourced from the theme on every call so they update if the
// theme is ever swapped at runtime.
ImU32 Group1EdgeColor() {
    return GetTheme().edge_group1;
}
ImU32 Group2EdgeColor() {
    return GetTheme().edge_group2;
}
// Dialectic challenge edges read as adversarial; use the attention color so the
// counter relationship is visually distinct from supporting/contextual edges.
ImU32 ChallengeEdgeColor() {
    return GetTheme().attention;
}

void ExpandRectToInclude(ImVec2 point, ImVec2& out_min, ImVec2& out_max) {
    out_min.x = std::min(out_min.x, point.x);
    out_min.y = std::min(out_min.y, point.y);
    out_max.x = std::max(out_max.x, point.x);
    out_max.y = std::max(out_max.y, point.y);
}

// ===== Arrowhead helpers =====

// Compute the two base corners of an arrowhead triangle given its tip,
// a unit direction vector, and side length.
void ComputeArrowBasePoints(ImVec2 tip, float dir_x, float dir_y, float size, ImVec2& out_left, ImVec2& out_right) {
    // Perpendicular to the direction vector
    float perp_x = -dir_y;
    float perp_y = dir_x;
    float half = size * 0.5f;
    out_left = ImVec2(tip.x - dir_x * size + perp_x * half, tip.y - dir_y * size + perp_y * half);
    out_right = ImVec2(tip.x - dir_x * size - perp_x * half, tip.y - dir_y * size - perp_y * half);
}

// Draw a solid (filled) arrowhead at 'tip' pointing in direction (dir_x, dir_y).
void DrawSolidArrow(ImDrawList* draw_list, ImVec2 tip, float dir_x, float dir_y, ImU32 color, float size = kArrowSize) {
    float length = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (length < 1.0f)
        return;
    dir_x /= length;
    dir_y /= length;
    ImVec2 base_left, base_right;
    ComputeArrowBasePoints(tip, dir_x, dir_y, size, base_left, base_right);
    draw_list->AddTriangleFilled(tip, base_left, base_right, color);
}

// Draw a hollow (outline-only) arrowhead at 'tip' pointing in direction (dir_x, dir_y).
void DrawHollowArrow(ImDrawList* draw_list,
                     ImVec2 tip,
                     float dir_x,
                     float dir_y,
                     ImU32 color,
                     float size = kArrowSize,
                     float thickness = kArrowOutlineWidth) {
    float length = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (length < 1.0f)
        return;
    dir_x /= length;
    dir_y /= length;
    ImVec2 base_left, base_right;
    ComputeArrowBasePoints(tip, dir_x, dir_y, size, base_left, base_right);
    draw_list->AddTriangle(tip, base_left, base_right, color, thickness);
}

// ===== Bezier curve helpers =====

// Draw a solid cubic Bezier curve.
void DrawSolidBezier(
    ImDrawList* draw_list, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, ImU32 color, float thickness = kSolidEdgeWidth) {
    draw_list->AddBezierCubic(p0, p1, p2, p3, color, thickness);
}

// Interpolate a point along a sampled polyline at the given arc-length distance.
ImVec2
InterpolateAtArcLength(const ImVec2* samples, const float* cumulative_lengths, int sample_count, float target_arc) {
    for (int i = 1; i <= sample_count; ++i) {
        if (cumulative_lengths[i] >= target_arc) {
            float segment_len = cumulative_lengths[i] - cumulative_lengths[i - 1];
            float fraction = (segment_len < 1e-6f) ? 0.0f : (target_arc - cumulative_lengths[i - 1]) / segment_len;
            return ImVec2(samples[i - 1].x + fraction * (samples[i].x - samples[i - 1].x),
                          samples[i - 1].y + fraction * (samples[i].y - samples[i - 1].y));
        }
    }
    return samples[sample_count];
}

// Draw a dashed cubic Bezier curve using arc-length parameterized sampling.
void DrawDashedBezier(ImDrawList* draw_list,
                      ImVec2 p0,
                      ImVec2 p1,
                      ImVec2 p2,
                      ImVec2 p3,
                      ImU32 color,
                      float thickness = kDashedEdgeWidth,
                      float dash_on = kDashLength,
                      float dash_off = kDashGap) {
    // Sample the curve into a polyline and compute cumulative arc lengths
    ImVec2 samples[kBezierSamples + 1];
    float cumulative_lengths[kBezierSamples + 1];
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
    if (total_arc < 1.0f)
        return;

    // Walk along the curve alternating between drawing and skipping
    float arc_pos = 0.0f;
    bool is_visible = true;
    while (arc_pos < total_arc) {
        float segment_end = arc_pos + (is_visible ? dash_on : dash_off);
        if (segment_end > total_arc)
            segment_end = total_arc;

        if (is_visible) {
            ImVec2 dash_start = InterpolateAtArcLength(samples, cumulative_lengths, kBezierSamples, arc_pos);
            ImVec2 dash_end = InterpolateAtArcLength(samples, cumulative_lengths, kBezierSamples, segment_end);
            draw_list->AddLine(dash_start, dash_end, color, thickness);
        }
        arc_pos = segment_end;
        is_visible = !is_visible;
    }
}

void DrawDashedLine(
    ImDrawList* draw_list, ImVec2 start, ImVec2 end, ImU32 color, float thickness, float dash_on, float dash_off) {
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 1.0f)
        return;

    float dir_x = dx / length;
    float dir_y = dy / length;
    float pos = 0.0f;
    bool is_visible = true;
    while (pos < length) {
        float segment_end = pos + (is_visible ? dash_on : dash_off);
        if (segment_end > length)
            segment_end = length;

        if (is_visible) {
            ImVec2 dash_start(start.x + dir_x * pos, start.y + dir_y * pos);
            ImVec2 dash_end(start.x + dir_x * segment_end, start.y + dir_y * segment_end);
            draw_list->AddLine(dash_start, dash_end, color, thickness);
        }

        pos = segment_end;
        is_visible = !is_visible;
    }
}

} // namespace

ImVec2 EvalBezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t) {
    float u = 1.0f - t;
    float uu = u * u, uuu = uu * u;
    float tt = t * t, ttt = tt * t;
    return ImVec2(uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x,
                  uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y);
}

void ComputeGroup1Endpoints(
    const LayoutNode& parent, const LayoutNode& child, ImVec2 origin, float zoom, ImVec2& out_start, ImVec2& out_end) {
    out_start = ImVec2(origin.x + (parent.position.x + parent.size.x * 0.5f) * zoom,
                       origin.y + (parent.position.y + parent.size.y) * zoom);
    out_end = ImVec2(origin.x + (child.position.x + child.size.x * 0.5f) * zoom, origin.y + child.position.y * zoom);
}

// Draw a Group1 (structural) edge: straight stubs -> solid Bezier -> solid arrowhead.
void DrawGroup1Edge(ImDrawList* draw_list, ImVec2 parent_bottom, ImVec2 child_top, float zoom) {
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float scaled_edge_width = kSolidEdgeWidth * scale;
    float scaled_arrow = kArrowSize * scale;

    ImVec2 stub_start(parent_bottom.x, parent_bottom.y + scaled_stub);
    ImVec2 stub_end(child_top.x, child_top.y - scaled_stub);

    float vertical_span = fabsf(stub_end.y - stub_start.y);
    ImVec2 ctrl_1(stub_start.x, stub_start.y + vertical_span * kVerticalControlPct);
    ImVec2 ctrl_2(stub_end.x, stub_end.y - vertical_span * kVerticalControlPct);

    ImU32 col = Group1EdgeColor();
    draw_list->AddLine(parent_bottom, stub_start, col, scaled_edge_width);
    DrawSolidBezier(draw_list, stub_start, ctrl_1, ctrl_2, stub_end, col, scaled_edge_width);
    draw_list->AddLine(stub_end, child_top, col, scaled_edge_width);
    DrawSolidArrow(draw_list, child_top, 0.0f, 1.0f, col, scaled_arrow);
}

void ComputeGroup1EdgeBounds(ImVec2 parent_bottom, ImVec2 child_top, float zoom, ImVec2& out_min, ImVec2& out_max) {
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float scaled_edge_width = kSolidEdgeWidth * scale;
    float scaled_arrow = kArrowSize * scale;

    ImVec2 stub_start(parent_bottom.x, parent_bottom.y + scaled_stub);
    ImVec2 stub_end(child_top.x, child_top.y - scaled_stub);
    float vertical_span = fabsf(stub_end.y - stub_start.y);
    ImVec2 ctrl_1(stub_start.x, stub_start.y + vertical_span * kVerticalControlPct);
    ImVec2 ctrl_2(stub_end.x, stub_end.y - vertical_span * kVerticalControlPct);

    out_min = parent_bottom;
    out_max = parent_bottom;
    ExpandRectToInclude(child_top, out_min, out_max);
    ExpandRectToInclude(stub_start, out_min, out_max);
    ExpandRectToInclude(stub_end, out_min, out_max);
    ExpandRectToInclude(ctrl_1, out_min, out_max);
    ExpandRectToInclude(ctrl_2, out_min, out_max);

    float pad = std::max(scaled_edge_width, scaled_arrow) + 1.0f;
    out_min.x -= pad;
    out_min.y -= pad;
    out_max.x += pad;
    out_max.y += pad;
}

// Compute screen-space endpoints for a Group2 (side-attached) edge.
// Parent side -> attachment nearest edge, depending on which side.
void ComputeGroup2Endpoints(const LayoutNode& parent,
                            const LayoutNode& attachment,
                            ImVec2 origin,
                            float zoom,
                            ImVec2& out_parent_side,
                            ImVec2& out_attachment_edge) {
    if (attachment.is_left_side) {
        out_parent_side =
            ImVec2(origin.x + parent.position.x * zoom, origin.y + (parent.position.y + parent.size.y * 0.5f) * zoom);
        out_attachment_edge = ImVec2(origin.x + (attachment.position.x + attachment.size.x) * zoom,
                                     origin.y + (attachment.position.y + attachment.size.y * 0.5f) * zoom);
    } else {
        out_parent_side = ImVec2(origin.x + (parent.position.x + parent.size.x) * zoom,
                                 origin.y + (parent.position.y + parent.size.y * 0.5f) * zoom);
        out_attachment_edge = ImVec2(origin.x + attachment.position.x * zoom,
                                     origin.y + (attachment.position.y + attachment.size.y * 0.5f) * zoom);
    }
}

// Draw a Group2 (contextual) edge: dashed stubs -> dashed Bezier -> hollow arrowhead.
void DrawGroup2Edge(ImDrawList* draw_list, ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom) {
    // Sign encodes horizontal direction: -1 toward left, +1 toward right
    float horizontal_sign = is_left_side ? -1.0f : 1.0f;
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float scaled_edge_width = kDashedEdgeWidth * scale;
    float scaled_dash = kDashLength * scale;
    float scaled_gap = kDashGap * scale;

    ImVec2 stub_start(parent_side.x + horizontal_sign * scaled_stub, parent_side.y);
    ImVec2 stub_end(attachment_edge.x - horizontal_sign * scaled_stub, attachment_edge.y);

    float horizontal_span = fabsf(stub_end.x - stub_start.x) * 0.5f;
    ImVec2 ctrl_1(stub_start.x + horizontal_sign * horizontal_span, stub_start.y);
    ImVec2 ctrl_2(stub_end.x - horizontal_sign * horizontal_span, stub_end.y);

    ImU32 col = Group2EdgeColor();
    DrawDashedLine(draw_list, parent_side, stub_start, col, scaled_edge_width, scaled_dash, scaled_gap);
    DrawDashedBezier(draw_list, stub_start, ctrl_1, ctrl_2, stub_end, col, scaled_edge_width, scaled_dash, scaled_gap);
    DrawDashedLine(draw_list, stub_end, attachment_edge, col, scaled_edge_width, scaled_dash, scaled_gap);
    // Arrow points into the attachment node
    DrawHollowArrow(
        draw_list, attachment_edge, horizontal_sign, 0.0f, col, kArrowSize * scale, kArrowOutlineWidth * scale);
}

void ComputeGroup2EdgeBounds(
    ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom, ImVec2& out_min, ImVec2& out_max) {
    float horizontal_sign = is_left_side ? -1.0f : 1.0f;
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float scaled_edge_width = kDashedEdgeWidth * scale;
    float scaled_arrow = kArrowSize * scale;

    ImVec2 stub_start(parent_side.x + horizontal_sign * scaled_stub, parent_side.y);
    ImVec2 stub_end(attachment_edge.x - horizontal_sign * scaled_stub, attachment_edge.y);
    float horizontal_span = fabsf(stub_end.x - stub_start.x) * 0.5f;
    ImVec2 ctrl_1(stub_start.x + horizontal_sign * horizontal_span, stub_start.y);
    ImVec2 ctrl_2(stub_end.x - horizontal_sign * horizontal_span, stub_end.y);

    out_min = parent_side;
    out_max = parent_side;
    ExpandRectToInclude(attachment_edge, out_min, out_max);
    ExpandRectToInclude(stub_start, out_min, out_max);
    ExpandRectToInclude(stub_end, out_min, out_max);
    ExpandRectToInclude(ctrl_1, out_min, out_max);
    ExpandRectToInclude(ctrl_2, out_min, out_max);

    float pad = std::max(scaled_edge_width, scaled_arrow) + 1.0f;
    out_min.x -= pad;
    out_min.y -= pad;
    out_max.x += pad;
    out_max.y += pad;
}

void DrawGroup1EdgeHighlight(ImDrawList* draw_list, ImVec2 parent_bottom, ImVec2 child_top, float zoom) {
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float thickness = 5.0f * scale;

    ImVec2 stub_start(parent_bottom.x, parent_bottom.y + scaled_stub);
    ImVec2 stub_end(child_top.x, child_top.y - scaled_stub);
    float vertical_span = fabsf(stub_end.y - stub_start.y);
    ImVec2 ctrl_1(stub_start.x, stub_start.y + vertical_span * kVerticalControlPct);
    ImVec2 ctrl_2(stub_end.x, stub_end.y - vertical_span * kVerticalControlPct);

    ImU32 color = WithAlpha(GetTheme().accent, 0.82f);
    draw_list->AddLine(parent_bottom, stub_start, color, thickness);
    draw_list->AddBezierCubic(stub_start, ctrl_1, ctrl_2, stub_end, color, thickness);
    draw_list->AddLine(stub_end, child_top, color, thickness);
}

// Draw a dialectic "Challenges" edge: a dashed straight line from the counter
// source to the target anchor, ending in a hollow (open) arrowhead at the target.
void DrawChallengeEdge(ImDrawList* draw_list, ImVec2 from, ImVec2 to, float zoom) {
    float scale = DpiScale() * zoom;
    float scaled_edge_width = kDashedEdgeWidth * scale;
    float scaled_dash = kDashLength * scale;
    float scaled_gap = kDashGap * scale;

    ImU32 col = ChallengeEdgeColor();
    DrawDashedLine(draw_list, from, to, col, scaled_edge_width, scaled_dash, scaled_gap);
    DrawHollowArrow(draw_list, to, to.x - from.x, to.y - from.y, col, kArrowSize * scale, kArrowOutlineWidth * scale);
}

void ComputeChallengeEdgeBounds(ImVec2 from, ImVec2 to, float zoom, ImVec2& out_min, ImVec2& out_max) {
    float scale = DpiScale() * zoom;
    float scaled_edge_width = kDashedEdgeWidth * scale;
    float scaled_arrow = kArrowSize * scale;

    out_min = from;
    out_max = from;
    ExpandRectToInclude(to, out_min, out_max);

    float pad = std::max(scaled_edge_width, scaled_arrow) + 1.0f;
    out_min.x -= pad;
    out_min.y -= pad;
    out_max.x += pad;
    out_max.y += pad;
}

void DrawGroup2EdgeHighlight(
    ImDrawList* draw_list, ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom) {
    float horizontal_sign = is_left_side ? -1.0f : 1.0f;
    float scale = DpiScale() * zoom;
    float scaled_stub = kStubLength * scale;
    float thickness = 4.5f * scale;

    ImVec2 stub_start(parent_side.x + horizontal_sign * scaled_stub, parent_side.y);
    ImVec2 stub_end(attachment_edge.x - horizontal_sign * scaled_stub, attachment_edge.y);
    float horizontal_span = fabsf(stub_end.x - stub_start.x) * 0.5f;
    ImVec2 ctrl_1(stub_start.x + horizontal_sign * horizontal_span, stub_start.y);
    ImVec2 ctrl_2(stub_end.x - horizontal_sign * horizontal_span, stub_end.y);

    ImU32 color = WithAlpha(GetTheme().accent, 0.78f);
    draw_list->AddLine(parent_side, stub_start, color, thickness);
    draw_list->AddBezierCubic(stub_start, ctrl_1, ctrl_2, stub_end, color, thickness);
    draw_list->AddLine(stub_end, attachment_edge, color, thickness);
}

} // namespace ui::gsn
