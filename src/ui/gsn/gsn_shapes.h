#pragma once

#include "ui/gsn/gsn_canvas.h" // for GsnNode

#include <imgui.h>

namespace ui::gsn {

// Geometry constants shared with text-layout code in `gsn_canvas.cpp`.
inline constexpr float kClaimRounding = 8.0f;     // corner rounding for rectangular Claim nodes
inline constexpr float kParallelogramSkew = 0.15f; // fraction of width used for skew inset

// Parallelogram (Strategy) shape with inward-skewed top/bottom edges.
void DrawParallelogram(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom);

// Stadium / capsule shape (Context, Assumption, Justification).
void DrawStadium(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom);

// Filled circle centered in the bounding box (Solution, Evidence).
void DrawCircle(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom);

// Rounded rectangle (Claim / default shape).
void DrawRoundedRect(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom);

// Diamond marker with optional "UND" label drawn below the node when undeveloped.
void DrawUndevelopedMarker(ImDrawList* draw_list,
                           const GsnNode& node,
                           ImVec2 top_left,
                           ImVec2 bottom_right,
                           float zoom);

// Pulsing outline drawn around nodes that are inside the active review scope.
void DrawReviewScopeHighlight(ImDrawList* draw_list,
                              ImVec2 top_left,
                              ImVec2 bottom_right,
                              float zoom,
                              bool primary);

} // namespace ui::gsn
