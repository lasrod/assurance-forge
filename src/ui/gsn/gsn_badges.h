#pragma once

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

// Draw the yellow "!" attention badge.
void DrawAttentionBadge(ImDrawList* draw_list, const BadgeRect& badge, float zoom);

// Draw the per-element AI/manual review status badge and its hover tooltip.
void DrawReviewBadge(ImDrawList* draw_list,
                     const BadgeRect& badge,
                     float zoom,
                     ElementReviewVisualStatus status,
                     const ElementReviewVisualState& state);

} // namespace ui::gsn
