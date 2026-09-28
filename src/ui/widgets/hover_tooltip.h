#pragma once

#include "imgui.h"

#include <string>

namespace ui::widgets {

// Shows `text` as a tooltip while the last item is hovered, disabled items
// included, since a disabled control is where a user most needs the reason.
// Does nothing when `text` is empty.
inline void TooltipOnHover(const std::string& text) {
    if (!text.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text.c_str());
}

} // namespace ui::widgets
