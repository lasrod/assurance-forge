#pragma once

#include "core/assurance_tree.h"
#include "imgui.h"
#include "parser/xml_parser.h"
#include "ui/element_context_menu.h"
#include "ui/gsn/gsn_model.h"
#include "ui/ui_state.h"

#include <string>
#include <vector>

namespace core {
class TerminologyService;
}

namespace sacm {
struct AssuranceCasePackage;
}

namespace ui::gsn {

struct GsnNode {
    std::string id;
    std::string type;
    ImVec2 position;
    ImVec2 size;
    std::string label;
    std::string label_secondary; // secondary language label
    bool undeveloped = false;
};

// Bold font used for the ID/name line in nodes (set by main.cpp at startup).
extern ImFont* g_BoldFont;

// Draw a single GSN node (rectangle + label) and handle clicks.
// canvas_origin is the fixed screen-space origin of the canvas.
// zoom scales all positions and sizes (default 1.0 = no zoom).
void DrawGsnNode(const GsnNode& node,
                 ImVec2 canvas_origin,
                 UiState& ui_state,
                 const parser::AssuranceCase* active_case,
                 const ElementContextActions& actions,
                 const core::TerminologyService* terminology_service = nullptr,
                 float zoom = 1.0f);

// Backwards-compatible function used by `main.cpp`.
// Internally uses the new `GsnCanvas` renderer.
void ShowGsnCanvasWindow();

// Render only the canvas content in the current window.
// This enables embedding the canvas under tab views.
void ShowGsnCanvasContent(UiState& ui_state,
                          const parser::AssuranceCase* active_case,
                          const ElementContextActions& actions,
                          const sacm::AssuranceCasePackage* terminology_package = nullptr);

// High-level renderer class (in implementation file)
class GsnCanvas;

// Provide a simple setter so external code (app) can push elements to the
// canvas renderer (legacy).
void SetCanvasElements(const std::vector<CanvasElement>& elements);

// Push an AssuranceTree to the canvas renderer (new).
void SetCanvasTree(const core::AssuranceTree& tree);

} // namespace ui::gsn
