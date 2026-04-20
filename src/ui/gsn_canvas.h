#pragma once

#include <string>
#include <vector>
#include "imgui.h"
#include "gsn_model.h"
#include "core/assurance_tree.h"

namespace ui {

struct GsnNode {
    std::string id;
    std::string type;
    ImVec2 position;
    ImVec2 size;
    std::string label;
    std::string label_secondary;  // secondary language label
};

// Bold font used for the ID/name line in nodes (set by main.cpp at startup).
extern ImFont* g_BoldFont;

// Draw a single GSN node (rectangle + label) and handle clicks.
// canvas_origin is the fixed screen-space origin of the canvas.
// zoom scales all positions and sizes (default 1.0 = no zoom).
void DrawGsnNode(const GsnNode& node, ImVec2 canvas_origin, float zoom = 1.0f);

// Backwards-compatible function used by `main.cpp`.
// Internally uses the new `GsnCanvas` renderer.
void ShowGsnCanvasWindow();

// High-level renderer class (in implementation file)
class GsnCanvas;

// Provide a simple setter so external code (app) can push elements to the
// canvas renderer (legacy).
void SetCanvasElements(const std::vector<CanvasElement>& elements);

// Push an AssuranceTree to the canvas renderer (new).
void SetCanvasTree(const core::AssuranceTree& tree);

} // namespace ui
