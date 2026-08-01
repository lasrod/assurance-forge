// Shared text truncation for narrow panels.
//
// Panels draw labels straight into the draw list (so hit-testing stays on the
// parent Selectable/TreeNode), which means the ImGui window clip rect cuts a
// too-long label mid-glyph with no ellipsis and no way to read the full value.
// These helpers cut on a UTF-8 boundary, append U+2026, and report whether the
// text was shortened so the caller can offer a tooltip.
#pragma once

#include "imgui.h"

#include <string>
#include <string_view>

namespace ui::widgets {

struct EllipsizedText {
    std::string text;       // What to draw; ends with U+2026 when truncated.
    float width = 0.0f;     // Rendered width of `text` at the current font size.
    bool truncated = false; // True when `text` is shorter than the input.
};

// Shortens `text` to fit `max_width` using the current font. A `max_width` at or
// below zero yields an empty result.
EllipsizedText Ellipsize(std::string_view text, float max_width);

// Draws `text` at `pos` (screen space) shortened to `max_width`. Returns true
// when it was truncated, so the caller can attach a tooltip carrying the full
// string.
bool AddTextEllipsized(ImDrawList* draw_list, ImVec2 pos, ImU32 color, std::string_view text, float max_width);

// Shows `full_text` in a tooltip when `truncated` is true and the last item is
// hovered. Call immediately after the item that owns the label.
void TooltipWhenTruncated(bool truncated, std::string_view full_text);

} // namespace ui::widgets
