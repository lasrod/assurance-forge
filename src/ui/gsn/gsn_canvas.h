#pragma once

#include "core/assurance_tree.h"
#include "core/terminology_package_service.h"
#include "imgui.h"
#include "core/sacm_model.h"
#include "ui/element_context_menu.h"
#include "ui/gsn/gsn_model.h"
#include "ui/ui_state.h"

#include <cstddef>
#include <functional>
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

enum class TerminologyCardKind { Resolved, Ambiguous, Undefined };

struct TerminologyCardCandidate {
    core::TerminologyPackageRef package_ref;
    core::TerminologyTermRef term_ref;
};

class GsnCanvas;

struct TerminologyCardState {
    bool pinned = false;
    bool clicked_term_this_frame = false;
    bool card_hovered_this_frame = false;
    TerminologyCardKind kind = TerminologyCardKind::Undefined;
    std::string element_id;
    std::string text;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
    core::TerminologyPackageRef package_ref;
    core::TerminologyTermRef term_ref;
    std::vector<TerminologyCardCandidate> candidates;
    ImVec2 anchor = ImVec2(0.0f, 0.0f);
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
                 const sacm::AssuranceCasePackage* terminology_package = nullptr,
                 TerminologyCardState* terminology_card_state = nullptr,
                 float zoom = 1.0f,
                 bool overlay_hovered = false,
                 struct TerminologyOccurrenceCache* terminology_occurrence_cache = nullptr);

void RenderPinnedTerminologyCard(TerminologyCardState& card_state,
                                 const sacm::AssuranceCasePackage* terminology_package,
                                 const ElementContextActions& actions);

// Backwards-compatible function used by `main.cpp`.
// Internally uses the new `GsnCanvas` renderer.
void ShowGsnCanvasWindow();

// Render only the canvas content in the current window.
// This enables embedding the canvas under tab views.
void ShowGsnCanvasContent(UiState& ui_state,
                          const parser::AssuranceCase* active_case,
                          const ElementContextActions& actions,
                          const sacm::AssuranceCasePackage* terminology_package = nullptr);

// Optional set of in-canvas overlay buttons rendered next to the zoom strip.
// The canvas keeps this layer-clean by carrying only primitives and callbacks;
// owners (app layer) interpret the actions. Presence of a callback signals
// that the corresponding button should be rendered.
struct CanvasOverlayButtons {
    bool history_enabled = true;
    bool history_active = false;
    const char* history_disabled_tooltip = nullptr;
    std::function<void()> on_toggle_history;     // Renders the history toggle when set.
    std::function<void()> on_return_to_live;     // Renders the return-to-live button when set.
};

void ShowGsnCanvasContentWithRenderer(GsnCanvas& renderer,
                                      UiState& ui_state,
                                      const parser::AssuranceCase* active_case,
                                      const ElementContextActions& actions,
                                      const sacm::AssuranceCasePackage* terminology_package = nullptr,
                                      const CanvasOverlayButtons* overlay_buttons = nullptr);

// Provide a simple setter so external code (app) can push elements to the
// canvas renderer (legacy).
void SetCanvasElements(const std::vector<CanvasElement>& elements);

// Push an AssuranceTree to the canvas renderer (new).
void SetCanvasTree(const core::AssuranceTree& tree);

} // namespace ui::gsn
