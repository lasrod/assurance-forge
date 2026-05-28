#pragma once

#include "core/acp/acp_relationship_index.h"
#include "core/assurance_tree.h"
#include "core/audit/history_highlights.h"
#include "core/sacm_model.h"
#include "ui/element_context_menu.h"
#include "ui/gsn/gsn_canvas.h"
#include "ui/gsn/gsn_model.h"
#include "ui/gsn/gsn_terminology_card.h"
#include "ui/ui_state.h"

#include <cstdint>
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
    // Set the monotonic revision counter of the underlying assurance case.
    // Used by Render() to invalidate per-frame caches (e.g. ACP relationship
    // targets) when the model has actually changed. Call next to SetTree().
    void SetCaseRevision(std::uint64_t revision) {
        case_revision_ = revision;
    }
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

    // History-timeline integration: when set, nodes whose id appears in the
    // map are overlaid with a colored border indicating the change kind
    // (Added=green, Modified=yellow, Deleted=red). The renderer never uses
    // this map for hit-testing — purely visual.
    void SetHistoryHighlights(std::unordered_map<std::string, core::audit::HistoryHighlightKind> highlights) {
        history_highlights_ = std::move(highlights);
    }
    void ClearHistoryHighlights() {
        history_highlights_.clear();
    }
    const std::unordered_map<std::string, core::audit::HistoryHighlightKind>& GetHistoryHighlights() const {
        return history_highlights_;
    }

    // Request that the next render center / fit-to-view a specific set of
    // node ids. Consumed on the next render call (which knows the viewport
    // size). Used by the History Timeline to keep the historical canvas
    // focused on whatever a slider transaction changed instead of leaving
    // the user staring at empty space. If `fit_all_fallback` is true and
    // none of the requested ids exist in the current layout, the renderer
    // instead fits every layout node into the viewport. Calling with an
    // empty set and `fit_all_fallback == true` performs a plain fit-all.
    void RequestFocusOnIds(std::unordered_set<std::string> ids, bool fit_all_fallback) {
        pending_focus_ids_ = std::move(ids);
        pending_focus_fit_all_fallback_ = fit_all_fallback;
        has_pending_focus_ = true;
    }
    bool HasPendingFocus() const {
        return has_pending_focus_;
    }
    // Internal: consume the pending focus request after applying it. Used
    // by `ShowGsnCanvasContentWithRenderer`. Returns true if a request was
    // pending and applied (caller supplies the actual viewport size).
    bool ConsumePendingFocus(ImVec2 viewport_size);

private:
    void RebuildNodeLookup();

    std::vector<CanvasElement> elements_;
    std::vector<LayoutNode> layout_nodes_;
    std::unordered_map<std::string, const LayoutNode*> node_by_id_;
    float zoom_level_ = 1.0f;
    ImVec2 view_offset_ = ImVec2(0.0f, 0.0f); // pixel-space pan offset
    TerminologyCardState terminology_card_state_;
    TerminologyOccurrenceCache terminology_occurrence_cache_;
    CanvasRenderStats last_render_stats_{};

    // Revision-counter cache for BuildAcpRelationshipTargets. Keyed on
    // (active_case pointer, case_revision). Valid only while the workbench
    // tab cache keeps the AssuranceCase storage stable across frames.
    std::uint64_t case_revision_ = 0;
    const parser::AssuranceCase* cached_acp_targets_case_ = nullptr;
    std::uint64_t cached_acp_targets_revision_ = ~std::uint64_t{0};
    std::vector<core::acp::AcpRelationshipTarget> cached_acp_targets_;

    // Optional per-element highlight overlay used by the History Timeline.
    std::unordered_map<std::string, core::audit::HistoryHighlightKind> history_highlights_;

    // Pending focus request set by `RequestFocusOnIds` and consumed inside
    // the render loop where the real viewport size is known.
    std::unordered_set<std::string> pending_focus_ids_;
    bool pending_focus_fit_all_fallback_ = false;
    bool has_pending_focus_ = false;
};

} // namespace ui::gsn
