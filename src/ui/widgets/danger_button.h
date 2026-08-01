// Button for an action that removes, deletes, discards or rejects something.
//
// Destructive and additive actions were styled identically, so "Delete Package"
// and "Add Term" carried the same visual weight and the only warning was the
// word itself. In a tool whose artifact is a safety argument, the control that
// destroys work should not look like the one that creates it.
//
// Deliberately an outline rather than a solid red block: these sit inside
// panels next to ordinary controls, and a filled danger button would dominate
// the panel and start reading as decoration once there are several.
#pragma once

#include "imgui.h"

namespace ui::widgets {

bool DangerButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));

} // namespace ui::widgets
