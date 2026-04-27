#pragma once

#include "ui/gsn/gsn_model.h"
#include "parser/xml_parser.h"
#include "ui/element_context_menu.h"
#include "ui/ui_state.h"
#include "core/assurance_tree.h"
#include <vector>
#include <unordered_set>
#include <string>

namespace ui::gsn {

class GsnCanvas {
public:
    GsnCanvas();
    // Set elements from tree (new — spec-compliant layout)
    void SetTree(const core::AssuranceTree& tree);
    // Set elements (legacy flat list)
    void SetElements(const std::vector<CanvasElement>& elements);
    // Render into the current ImGui window/child
    void Render(UiState& ui_state,
                const parser::AssuranceCase* active_case,
                const ElementContextActions& actions);

    // Zoom controls
    void ZoomIn();
    void ZoomOut();
    void ResetZoom();
    float GetZoom() const { return zoom_level_; }
    // Zoom toward a specific content-space point (keeps that point stationary on screen).
    void ZoomAtPoint(float new_zoom, ImVec2 focus_content);

    // Pan the view by a pixel-space delta.
    void Pan(float dx, float dy);
    ImVec2 GetViewOffset() const { return view_offset_; }

    // Center the view on a specific node by ID. Returns true if found.
    bool CenterOnNode(const std::string& node_id, ImVec2 viewport_size);

    // Fit-to-view the AABB of every layout node whose id is in `ids`. Adjusts
    // both `view_offset_` and `zoom_level_` so the AABB is centered with a
    // small padding. Returns true if at least one matching node was found.
    bool CenterOnIds(const std::unordered_set<std::string>& ids,
                     ImVec2 viewport_size);

    // Get the bounding box of all content in layout-space (unzoomed).
    // Returns min and max corners. If no nodes, both are (0,0).
    void GetContentBounds(ImVec2& out_min, ImVec2& out_max) const;

private:
    std::vector<CanvasElement> elements_;
    std::vector<LayoutNode> layout_nodes_;
    float zoom_level_ = 1.0f;
    ImVec2 view_offset_ = ImVec2(0.0f, 0.0f); // pixel-space pan offset
};

} // namespace ui::gsn
