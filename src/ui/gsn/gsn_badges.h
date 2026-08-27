#pragma once

#include "core/problems/problem_attention.h"
#include "ui/ui_state.h"

#include <imgui.h>

namespace ui::gsn {

// Screen-space rectangle for a single badge slot positioned above a GSN node.
struct BadgeRect {
    ImVec2 min;
    ImVec2 max;
};

// Compute the screen-space rectangle for badge `slot` (0-based) in a row of
// `slot_count` badges horizontally centered above the node bounding box.
BadgeRect ComputeBadgeRect(ImVec2 top_left, ImVec2 bottom_right, float zoom, int slot, int slot_count);

// True if `mouse_pos` (typically `ImGui::GetIO().MousePos`) is inside `badge`.
bool IsPointInsideBadge(const BadgeRect& badge, ImVec2 point);

// Draw the unified problem alert badge. Icon and colour reflect the highest
// severity in `summary`: Error -> red x, Warning -> orange !, Info -> blue i.
// The hover tooltip summarises the top problem and total count.
void DrawProblemBadge(ImDrawList* draw_list,
                      const BadgeRect& badge,
                      float zoom,
                      const core::ElementBadgeSummary& summary);

// Draw the AI-in-progress spinner overlay. Rendered in its own slot so it can
// co-exist with a problem badge for the same element.
void DrawAiSpinnerBadge(ImDrawList* draw_list, const BadgeRect& badge, float zoom);

// Draw a persistent successful-review check badge. The marker is derived from
// saved review state rather than the transient completion event.
void DrawAiSuccessBadge(ImDrawList* draw_list, const BadgeRect& badge, float zoom, const AiReviewSuccessMarker& marker);

// Draw the link badge of a piece of evidence whose location is recorded. The
// tooltip names the location; the click target is the caller's, like the
// problem badge's.
void DrawLinkBadge(ImDrawList* draw_list, const BadgeRect& badge, float zoom, const std::string& location);

} // namespace ui::gsn
