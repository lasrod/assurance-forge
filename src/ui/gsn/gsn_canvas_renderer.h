#pragma once

#include "core/assurance_tree.h"
#include "core/sacm_model.h"
#include "ui/element_context_menu.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_model.h"
#include "ui/ui_state.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sacm {
struct AssuranceCasePackage;
}

namespace ui::gsn {

struct CanvasRenderStats {
    int nodes_drawn = 0;
    int nodes_culled = 0;
    int edges_drawn = 0;
    int edges_culled = 0;
    int shadows_drawn = 0;
    int interior_shading_drawn = 0;
    int selection_glow_drawn = 0;
    int acp_decorators_drawn = 0;
    int terminology_spans_drawn = 0;
    int terminology_tokens_scanned = 0;
    int clip_rect_pushes = 0;
    int draw_list_vtx = 0;
    int draw_list_idx = 0;
    int draw_list_cmds = 0;
    bool relationship_context_menu_active = false;
};

// Returns stats from the most recent GSN canvas render pass.
CanvasRenderStats GetLastCanvasRenderStats();

// Pointer to the CanvasRenderStats accumulator for the currently-rendering
// canvas, if any. Used by draw helpers (shapes, badges, terminology) to bump
// fine-grained counters without plumbing a stats pointer through every call.
// Set to non-null only between `GsnCanvas::Render` enter and exit.
CanvasRenderStats* CurrentRenderStats();
void SetCurrentRenderStats(CanvasRenderStats* stats);

// Snapshot of the most recent frame's stats from whichever GsnCanvas
// instance rendered last. Populated at the end of GsnCanvas::Render.
extern CanvasRenderStats g_last_render_stats_snapshot;

class GsnCanvas {
public:
    GsnCanvas();
    // Set elements from tree (new — spec-compliant layout)
    void SetTree(const core::AssuranceTree& tree);
    // Set elements (legacy flat list)
    void SetElements(const std::vector<CanvasElement>& elements);
    // Render into the current ImGui window/child.
    // `overlay_hovered` should be `true` when the mouse is over a floating
    // canvas overlay (zoom buttons, language toggle) so that node clicks and
    // hovers are suppressed accordingly. Computed by the host frame before
    // this call.
    void Render(UiState& ui_state,
                const parser::AssuranceCase* active_case,
                const ElementContextActions& actions,
                const sacm::AssuranceCasePackage* terminology_package = nullptr,
                bool overlay_hovered = false);

    // Zoom controls
    void ZoomIn();
    void ZoomOut();
    void ResetZoom();
    float GetZoom() const {
        return zoom_level_;
    }
    // Zoom toward a specific content-space point (keeps that point stationary on screen).
    void ZoomAtPoint(float new_zoom, ImVec2 focus_content);

    // Pan the view by a pixel-space delta.
    void Pan(float dx, float dy);
    ImVec2 GetViewOffset() const {
        return view_offset_;
    }

    // Center the view on a specific node by ID. Returns true if found.
    bool CenterOnNode(const std::string& node_id, ImVec2 viewport_size);

    // Fit-to-view the AABB of every layout node whose id is in `ids`. Adjusts
    // both `view_offset_` and `zoom_level_` so the AABB is centered with a
    // small padding. Returns true if at least one matching node was found.
    bool CenterOnIds(const std::unordered_set<std::string>& ids, ImVec2 viewport_size);

    // Get the bounding box of all content in layout-space (unzoomed).
    // Returns min and max corners. If no nodes, both are (0,0).
    void GetContentBounds(ImVec2& out_min, ImVec2& out_max) const;

    // Returns the stats from the most recent Render() call for this canvas.
    CanvasRenderStats GetLastRenderStats() const {
        return last_render_stats_;
    }

private:
    void RebuildNodeLookup();

    std::vector<CanvasElement> elements_;
    std::vector<LayoutNode> layout_nodes_;
    std::unordered_map<std::string, const LayoutNode*> node_by_id_;
    float zoom_level_ = 1.0f;
    ImVec2 view_offset_ = ImVec2(0.0f, 0.0f); // pixel-space pan offset
    TerminologyCardState terminology_card_state_;
    CanvasRenderStats last_render_stats_{};
};

} // namespace ui::gsn
