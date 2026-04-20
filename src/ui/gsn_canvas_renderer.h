#pragma once

#include "gsn_model.h"
#include "core/assurance_tree.h"
#include <vector>

namespace ui {

class GsnCanvas {
public:
    GsnCanvas();
    // Set elements from tree (new — spec-compliant layout)
    void SetTree(const core::AssuranceTree& tree);
    // Set elements (legacy flat list)
    void SetElements(const std::vector<CanvasElement>& elements);
    // Render into the current ImGui window/child
    void Render();

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

    // Get the bounding box of all content in layout-space (unzoomed).
    // Returns min and max corners. If no nodes, both are (0,0).
    void GetContentBounds(ImVec2& out_min, ImVec2& out_max) const;

private:
    std::vector<CanvasElement> elements_;
    std::vector<LayoutNode> layout_nodes_;
    float zoom_level_ = 1.0f;
    ImVec2 view_offset_ = ImVec2(0.0f, 0.0f); // pixel-space pan offset
};

} // namespace ui
