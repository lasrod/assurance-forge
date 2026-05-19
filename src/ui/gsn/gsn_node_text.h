#pragma once

#include "ui/gsn/gsn_canvas.h" // GsnNode, g_BoldFont
#include "ui/ui_state.h"       // UiState

#include <imgui.h>

namespace ui::gsn {

// Shared text-layout constants. Exposed here (not in the .cpp anon namespace)
// because `gsn_canvas.cpp`'s terminology-span renderer also references them.
inline constexpr float kTextPadding = 6.0f;    // padding between shape edge and text
inline constexpr float kFullLabelZoom = 0.75f; // below this, only the first line of the label is drawn

// Compute the horizontal text region (left edge + wrap width) for a node's
// shape. All outputs are in screen-space and already scaled by `zoom`.
void ComputeTextRegion(const GsnNode& node,
                       ImVec2 top_left,
                       ImVec2 bottom_right,
                       float zoom,
                       bool reserve_attention_badge,
                       float& out_text_left,
                       float& out_text_wrap);

// Draw the node label: bold first line (ID: Name), normal text for the rest
// (description). The block is vertically centered within the node bounding box.
void DrawNodeLabel(ImDrawList* draw_list,
                   const GsnNode& node,
                   ImVec2 top_left,
                   ImVec2 bottom_right,
                   float text_left,
                   float text_wrap,
                   float zoom,
                   ImU32 ink_color,
                   const UiState& ui_state);

} // namespace ui::gsn
