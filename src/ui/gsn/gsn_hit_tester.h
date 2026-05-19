#pragma once

#include "core/acp/acp_relationship_index.h"
#include "ui/gsn/gsn_layout.h"
#include "ui/ui_state.h"

#include <imgui.h>

#include <string>
#include <unordered_map>
#include <vector>

// Mouse-picking and edge-key utilities for the GSN canvas. Hit-test geometry
// mirrors the paths produced by `gsn_edge_renderer` so picking matches what
// the user sees.

namespace ui::gsn {

// Stable composite key for a (parent, child) relationship edge.
std::string EdgeKey(const std::string& parent_id, const std::string& child_id);

// AABB intersection test, used by viewport culling and edge bbox checks.
bool RectsIntersect(ImVec2 a_min, ImVec2 a_max, ImVec2 b_min, ImVec2 b_max);

// True if `target` is the currently-selected relationship and `edge_key`
// matches the selected edge instance.
bool RelationshipEdgeSelected(const UiState& ui_state,
                              const core::acp::AcpRelationshipTarget* target,
                              const std::string& edge_key);

// Test whether a screen-space point lies inside a layout node's rect.
bool PointInsideNode(ImVec2 point, const LayoutNode& node, ImVec2 origin, float zoom);

// Pick the relationship edge under the current mouse position, returning its
// `EdgeKey(parent, child)`. Returns empty string when nothing is under the
// cursor, when the cursor sits on top of a node, or when the canvas isn't
// hovered. `cull_min`/`cull_max` is the viewport rect used to discard far-off
// edges before precise distance testing.
std::string PickRelationshipEdge(const std::vector<LayoutNode>& layout_nodes,
                                 const std::unordered_map<std::string, const LayoutNode*>& node_by_id,
                                 ImVec2 origin,
                                 float zoom,
                                 ImVec2 cull_min,
                                 ImVec2 cull_max);

} // namespace ui::gsn
