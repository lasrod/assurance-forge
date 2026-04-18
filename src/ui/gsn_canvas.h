#pragma once

#include <string>
#include "imgui.h"

namespace ui {

struct GsnNode {
    std::string id;
    std::string type;
    ImVec2 position;
    ImVec2 size;
    std::string label;
};

// Draw a single GSN node (rectangle + label) and handle clicks.
void DrawGsnNode(const GsnNode& node);

// Show the dockable GSN Canvas window and draw hardcoded nodes.
void ShowGsnCanvasWindow();

} // namespace ui
