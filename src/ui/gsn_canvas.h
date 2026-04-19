#pragma once

#include <string>
#include <vector>
#include "imgui.h"
#include "gsn_model.h"

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

// Backwards-compatible function used by `main.cpp`.
// Internally uses the new `GsnCanvas` renderer.
void ShowGsnCanvasWindow();

// High-level renderer class (in implementation file)
class GsnCanvas;

// Provide a simple setter so external code (app) can push elements to the
// canvas renderer.
void SetCanvasElements(const std::vector<CanvasElement>& elements);

} // namespace ui
