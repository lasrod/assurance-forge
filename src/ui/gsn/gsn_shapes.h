#pragma once

#include "core/changesets/change_set.h"
#include "ui/gsn/gsn_canvas.h" // for GsnNode

#include <imgui.h>

namespace ui::gsn {

// Geometry constants shared with text-layout code in `gsn_canvas.cpp`.
inline constexpr float kClaimRounding = 8.0f;      // corner rounding for rectangular Claim nodes
inline constexpr float kContextRounding = 24.0f;   // corner rounding for Context/Assumption/Justification nodes
inline constexpr float kParallelogramSkew = 0.15f; // fraction of width used for skew inset

// Parallelogram (Strategy) shape with inward-skewed top/bottom edges.
void DrawParallelogram(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom);

// Rounded rectangle with a softer corner radius (Context, Assumption, Justification).
void DrawStadium(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom);

// Filled circle centered in the bounding box (Solution, Evidence).
void DrawCircle(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom);

// Rounded rectangle (Claim / default shape).
void DrawRoundedRect(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, ImU32 fill_color, float zoom);

// GSN element-abstraction marker below the node: a hollow diamond for
// undeveloped, a hollow triangle for uninstantiated, or the standard overlaid
// diamond with a horizontal divider when both apply.
void DrawElementAbstractionMarker(
    ImDrawList* draw_list, const GsnNode& node, ImVec2 top_left, ImVec2 bottom_right, float zoom);

// Pulsing outline drawn around nodes that are inside the active review scope.
void DrawReviewScopeHighlight(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, float zoom, bool primary);

// Mark a GSN v3 dialectic counter element (counter argument / counter evidence)
// as a raised challenge that needs resolving. Uses color-independent cues so it
// is distinguishable regardless of color vision: a bold dashed border hugging
// the shape plus a warning ("!") badge pinned to the node's top-left corner.
// `circular` traces the dashed border around the inscribed circle for round
// Solution/Evidence shapes, otherwise around the rounded rectangle.
void DrawCounterChallengeDecoration(
    ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right, bool circular, float zoom);

// Mark a node that a connected AI client's open change set would add, change or
// remove, so the user watches the argument take shape instead of being handed a
// finished proposal to decode.
//
// Follows the same rule as the counter decoration: the cue is a border *and* a
// short badge, never colour alone, so what is being proposed is legible to
// someone who cannot distinguish the colours. `circular` traces round
// Solution/Evidence shapes rather than the rounded rectangle.
void DrawProposedChangeDecoration(ImDrawList* draw_list, ImVec2 top_left, ImVec2 bottom_right,
                                  bool circular, float zoom,
                                  core::changesets::ElementChange change);

} // namespace ui::gsn
