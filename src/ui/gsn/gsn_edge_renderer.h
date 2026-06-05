#pragma once

#include "ui/gsn/gsn_layout.h"

#include <imgui.h>

// Edge rendering primitives extracted from `gsn_canvas_renderer.cpp`.
//
// "Group1" edges are structural (parent -> child) — solid Bezier with a filled
// arrowhead, drawn top-to-bottom between node bodies.
// "Group2" edges are contextual (parent -> side-attached node) — dashed Bezier
// with a hollow arrowhead, drawn horizontally to the attachment.
//
// All draw functions take screen-space coordinates and a zoom factor; endpoint
// computation maps from layout-space (LayoutNode positions) to screen-space.

namespace ui::gsn {

// Geometry constants shared with hit-testing so picking traces the exact path
// the renderer drew. Keep these in sync if DrawGroup* logic changes.
inline constexpr float kStubLength = 12.0f;        // straight segment at each end of a Bezier curve
inline constexpr float kVerticalControlPct = 0.4f; // Bezier control point distance (fraction of vertical span)
inline constexpr int kBezierSamples = 64;          // arc-length sample count along a Bezier curve

// Cubic Bezier evaluation at parameter t ∈ [0, 1]. Exposed for hit-testing.
ImVec2 EvalBezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t);

// Compute screen-space endpoints for a Group1 (structural) edge.
void ComputeGroup1Endpoints(
    const LayoutNode& parent, const LayoutNode& child, ImVec2 origin, float zoom, ImVec2& out_start, ImVec2& out_end);

// Compute screen-space endpoints for a Group2 (side-attached) edge.
void ComputeGroup2Endpoints(const LayoutNode& parent,
                            const LayoutNode& attachment,
                            ImVec2 origin,
                            float zoom,
                            ImVec2& out_parent_side,
                            ImVec2& out_attachment_edge);

// The five-point screen-space path of a Group1 (structural) edge. `straight_drop`
// (screen px) is how far the edge runs straight down from the parent before the
// Bezier curves out to the child — used to clear a tall side challenge tree.
struct Group1EdgePath {
    ImVec2 parent_bottom;
    ImVec2 stub_start;
    ImVec2 ctrl_1;
    ImVec2 ctrl_2;
    ImVec2 stub_end;
    ImVec2 child_top;
};
Group1EdgePath ComputeGroup1Path(ImVec2 parent_bottom, ImVec2 child_top, float zoom, float straight_drop = 0.0f);

// Compute an AABB (with padding) covering everything DrawGroup1Edge would draw.
void ComputeGroup1EdgeBounds(
    ImVec2 parent_bottom, ImVec2 child_top, float zoom, ImVec2& out_min, ImVec2& out_max, float straight_drop = 0.0f);

// Compute an AABB (with padding) covering everything DrawGroup2Edge would draw.
void ComputeGroup2EdgeBounds(
    ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom, ImVec2& out_min, ImVec2& out_max);

// Draw a Group1 structural edge between two node anchor points.
void DrawGroup1Edge(ImDrawList* draw_list, ImVec2 parent_bottom, ImVec2 child_top, float zoom, float straight_drop = 0.0f);

// Draw a Group2 contextual edge between a parent side and an attachment side.
void DrawGroup2Edge(ImDrawList* draw_list, ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom);

// Draw a thick, semi-transparent highlight along the Group1 edge path
// (used to mark the currently-selected relationship).
void DrawGroup1EdgeHighlight(
    ImDrawList* draw_list, ImVec2 parent_bottom, ImVec2 child_top, float zoom, float straight_drop = 0.0f);

// Draw a thick, semi-transparent highlight along the Group2 edge path.
void DrawGroup2EdgeHighlight(
    ImDrawList* draw_list, ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom);

// Draw a GSN v3 dialectic "Challenges" edge: a dashed straight line from the
// counter source (`from`) to the challenged target anchor (`to`), with a hollow
// (open) arrowhead at `to` pointing at the target.
void DrawChallengeEdge(ImDrawList* draw_list, ImVec2 from, ImVec2 to, float zoom);

// Compute an AABB (with padding) covering everything DrawChallengeEdge would draw.
void ComputeChallengeEdgeBounds(ImVec2 from, ImVec2 to, float zoom, ImVec2& out_min, ImVec2& out_max);

// Thick, semi-transparent highlight along a challenge edge (hover/selection cue).
void DrawChallengeEdgeHighlight(ImDrawList* draw_list, ImVec2 from, ImVec2 to, float zoom);

} // namespace ui::gsn
