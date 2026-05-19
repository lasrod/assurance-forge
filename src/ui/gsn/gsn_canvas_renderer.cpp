#include "ui/gsn/gsn_canvas_renderer.h"

#include "core/acp/acp_relationship_index.h"
#include "core/perf/frame_profiler.h"
#include "core/terminology_scope_service.h"
#include "ui/gsn/gsn_acp_decorator.h"
#include "ui/gsn/gsn_canvas.h" // for DrawGsnNode
#include "ui/gsn/gsn_dpi.h"
#include "ui/gsn/gsn_edge_renderer.h"
#include "ui/gsn/gsn_hit_tester.h"
#include "ui/gsn/gsn_layout.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <imgui.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui::gsn {

// Viewport / scroll constants. Edge-drawing constants live in gsn_edge_renderer.{h,cpp}.
static constexpr float kScrollPadding = 40.0f; // extra padding beyond outermost node for scrolling
static constexpr float kCullMarginPx = 120.0f; // screen-space culling margin around viewport

// EdgeKey, RectsIntersect, RelationshipEdgeSelected, PointInsideNode, and
// PickRelationshipEdge are implemented in `ui/gsn/gsn_hit_tester.{h,cpp}`.

// ===== Zoom constants =====
static constexpr float kZoomMin = 0.25f; // minimum zoom level (25%)
static constexpr float kZoomMax = 3.0f;  // maximum zoom level (300%)
static constexpr float kZoomStep = 0.1f; // zoom increment per step (10%)

GsnCanvas::GsnCanvas() {}

void GsnCanvas::RebuildNodeLookup() {
    node_by_id_.clear();
    node_by_id_.reserve(layout_nodes_.size());
    for (const auto& node : layout_nodes_) {
        node_by_id_[node.id] = &node;
    }
}

void GsnCanvas::SetTree(const core::AssuranceTree& tree) {
    LayoutEngine le;
    layout_nodes_ = le.ComputeLayout(tree);
    RebuildNodeLookup();
}

void GsnCanvas::SetElements(const std::vector<CanvasElement>& elements) {
    elements_ = elements;
    LayoutEngine le;
    layout_nodes_ = le.ComputeLayout(elements_);
    RebuildNodeLookup();
}

void GsnCanvas::ZoomIn() {
    zoom_level_ = std::min(zoom_level_ + kZoomStep, kZoomMax);
}

void GsnCanvas::ZoomOut() {
    zoom_level_ = std::max(zoom_level_ - kZoomStep, kZoomMin);
}

void GsnCanvas::ResetZoom() {
    zoom_level_ = 1.0f;
}

void GsnCanvas::ZoomAtPoint(float new_zoom, ImVec2 focus_content) {
    float old_zoom = zoom_level_;
    zoom_level_ = std::max(kZoomMin, std::min(new_zoom, kZoomMax));
    // Adjust view_offset_ so the content point under the mouse stays fixed on screen.
    // screen_pos = canvas_origin + content * zoom - view_offset
    // We want the same screen_pos before and after:
    //   view_offset_new = view_offset_old + focus * (new_zoom - old_zoom)
    view_offset_.x += focus_content.x * (zoom_level_ - old_zoom);
    view_offset_.y += focus_content.y * (zoom_level_ - old_zoom);
}

void GsnCanvas::Pan(float dx, float dy) {
    view_offset_.x += dx;
    view_offset_.y += dy;
}

void GsnCanvas::GetContentBounds(ImVec2& out_min, ImVec2& out_max) const {
    if (layout_nodes_.empty()) {
        out_min = ImVec2(0, 0);
        out_max = ImVec2(0, 0);
        return;
    }
    float min_x = FLT_MAX, min_y = FLT_MAX;
    float max_x = -FLT_MAX, max_y = -FLT_MAX;
    for (const auto& node : layout_nodes_) {
        if (node.position.x < min_x)
            min_x = node.position.x;
        if (node.position.y < min_y)
            min_y = node.position.y;
        float r = node.position.x + node.size.x;
        float b = node.position.y + node.size.y;
        if (r > max_x)
            max_x = r;
        if (b > max_y)
            max_y = b;
    }
    // Add some padding around the content
    float pad = DpiSize(kScrollPadding);
    out_min = ImVec2(min_x - pad, min_y - pad);
    out_max = ImVec2(max_x + pad, max_y + pad);
}

// ===== Edge drawing helpers =====
// Group1/Group2 edge endpoint computation, drawing, bounds, and highlights are
// implemented in `ui/gsn/gsn_edge_renderer.{h,cpp}` and used below via the
// `ui::gsn` namespace.

// ===== Main rendering =====

void GsnCanvas::Render(UiState& ui_state,
                       const parser::AssuranceCase* active_case,
                       const ElementContextActions& actions,
                       const sacm::AssuranceCasePackage* terminology_package,
                       bool overlay_hovered) {
    core::perf::ScopedTimer perf_scope_render("gsn.render");
    CanvasRenderStats frame_stats{};
    SetCurrentRenderStats(&frame_stats);

    std::optional<core::TerminologyService> terminology_service;
    if (terminology_package) {
        core::perf::ScopedTimer perf_scope_term("gsn.terminology_service_build");
        terminology_service.emplace(*terminology_package);
    }
    const core::TerminologyService* terminology_service_ptr =
        terminology_service.has_value() ? &terminology_service.value() : nullptr;
    terminology_card_state_.clicked_term_this_frame = false;
    terminology_card_state_.card_hovered_this_frame = false;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    float zoom = zoom_level_;

    // Apply our own view offset to the drawing origin (replaces ImGui scroll)
    ImVec2 origin(canvas_pos.x - view_offset_.x, canvas_pos.y - view_offset_.y);

    const float cull_margin = DpiSize(kCullMarginPx);
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 content_min = ImGui::GetWindowContentRegionMin();
    ImVec2 content_max = ImGui::GetWindowContentRegionMax();
    ImVec2 viewport_min(window_pos.x + content_min.x, window_pos.y + content_min.y);
    ImVec2 viewport_max(window_pos.x + content_max.x, window_pos.y + content_max.y);
    ImVec2 cull_min(viewport_min.x - cull_margin, viewport_min.y - cull_margin);
    ImVec2 cull_max(viewport_max.x + cull_margin, viewport_max.y + cull_margin);

    const std::vector<core::acp::AcpRelationshipTarget> acp_targets =
        active_case ? [&]() {
            core::perf::ScopedTimer perf_scope_acp("gsn.acp.build_targets");
            return core::acp::BuildAcpRelationshipTargets(*active_case);
        }()
                    : std::vector<core::acp::AcpRelationshipTarget>{};
    const auto acp_target_by_edge = [&]() {
        core::perf::ScopedTimer perf_scope("gsn.acp.target_lookup");
        return BuildRelationshipTargetLookup(acp_targets);
    }();
    const auto acp_by_relationship = [&]() {
        core::perf::ScopedTimer perf_scope("gsn.acp.relationship_lookup");
        return BuildRelationshipAcpLookup(active_case);
    }();
    const auto acp_by_element = [&]() {
        core::perf::ScopedTimer perf_scope("gsn.acp.element_lookup");
        return BuildElementAcpLookup(active_case);
    }();
    const std::string picked_edge_key = [&]() {
        core::perf::ScopedTimer perf_scope("gsn.pick_edge");
        return PickRelationshipEdge(layout_nodes_, node_by_id_, origin, zoom, viewport_min, viewport_max);
    }();

    // Draw edges first (beneath nodes)
    {
        core::perf::ScopedTimer perf_scope_edges("gsn.edges");
        for (const auto& child_node : layout_nodes_) {
            if (child_node.parent_id.empty())
                continue;

            auto parent_it = node_by_id_.find(child_node.parent_id);
            if (parent_it == node_by_id_.end())
                continue;
            const LayoutNode& parent_node = *parent_it->second;

            if (child_node.group == ElementGroup::Group2) {
                ImVec2 parent_side, attachment_edge;
                ComputeGroup2Endpoints(parent_node, child_node, origin, zoom, parent_side, attachment_edge);
                ImVec2 edge_min, edge_max;
                ComputeGroup2EdgeBounds(
                    parent_side, attachment_edge, child_node.is_left_side, zoom, edge_min, edge_max);
                if (!RectsIntersect(edge_min, edge_max, cull_min, cull_max)) {
                    ++frame_stats.edges_culled;
                    continue;
                }
                DrawGroup2Edge(draw_list, parent_side, attachment_edge, child_node.is_left_side, zoom);
                const std::string edge_key = EdgeKey(parent_node.id, child_node.id);
                const auto target_it = acp_target_by_edge.find(edge_key);
                const core::acp::AcpRelationshipTarget* acp_target =
                    target_it == acp_target_by_edge.end() ? nullptr : target_it->second;
                const std::vector<parser::AcpRecord>* edge_acps = nullptr;
                if (acp_target) {
                    const auto acp_it = acp_by_relationship.find(acp_target->relationship_id);
                    if (acp_it != acp_by_relationship.end())
                        edge_acps = &acp_it->second;
                }
                if (RelationshipEdgeSelected(ui_state, acp_target, edge_key))
                    DrawGroup2EdgeHighlight(draw_list, parent_side, attachment_edge, child_node.is_left_side, zoom);
                frame_stats.relationship_context_menu_active =
                    RenderAcpRelationshipContextMenu(acp_target,
                                                     edge_acps,
                                                     actions,
                                                     ui_state,
                                                     edge_key,
                                                     parent_node.id,
                                                     child_node.id,
                                                     picked_edge_key == edge_key) ||
                    frame_stats.relationship_context_menu_active;
                if (acp_target && acp_target->eligible_for_acp && edge_acps) {
                    DrawAcpRelationshipDecorator(
                        draw_list,
                        ImVec2((parent_side.x + attachment_edge.x) * 0.5f, (parent_side.y + attachment_edge.y) * 0.5f),
                        zoom,
                        *acp_target,
                        *edge_acps,
                        actions,
                        ui_state);
                }
                ++frame_stats.edges_drawn;
            } else {
                ImVec2 parent_bottom, child_top;
                ComputeGroup1Endpoints(parent_node, child_node, origin, zoom, parent_bottom, child_top);
                ImVec2 edge_min, edge_max;
                ComputeGroup1EdgeBounds(parent_bottom, child_top, zoom, edge_min, edge_max);
                if (!RectsIntersect(edge_min, edge_max, cull_min, cull_max)) {
                    ++frame_stats.edges_culled;
                    continue;
                }
                DrawGroup1Edge(draw_list, parent_bottom, child_top, zoom);
                const std::string edge_key = EdgeKey(parent_node.id, child_node.id);
                const auto target_it = acp_target_by_edge.find(edge_key);
                const core::acp::AcpRelationshipTarget* acp_target =
                    target_it == acp_target_by_edge.end() ? nullptr : target_it->second;
                const std::vector<parser::AcpRecord>* edge_acps = nullptr;
                if (acp_target) {
                    const auto acp_it = acp_by_relationship.find(acp_target->relationship_id);
                    if (acp_it != acp_by_relationship.end())
                        edge_acps = &acp_it->second;
                }
                if (RelationshipEdgeSelected(ui_state, acp_target, edge_key))
                    DrawGroup1EdgeHighlight(draw_list, parent_bottom, child_top, zoom);
                frame_stats.relationship_context_menu_active =
                    RenderAcpRelationshipContextMenu(acp_target,
                                                     edge_acps,
                                                     actions,
                                                     ui_state,
                                                     edge_key,
                                                     parent_node.id,
                                                     child_node.id,
                                                     picked_edge_key == edge_key) ||
                    frame_stats.relationship_context_menu_active;
                if (acp_target && acp_target->eligible_for_acp && edge_acps) {
                    DrawAcpRelationshipDecorator(
                        draw_list,
                        ImVec2((parent_bottom.x + child_top.x) * 0.5f, (parent_bottom.y + child_top.y) * 0.5f),
                        zoom,
                        *acp_target,
                        *edge_acps,
                        actions,
                        ui_state);
                }
                ++frame_stats.edges_drawn;
            }
        }
    } // gsn.edges

    // Draw nodes on top of edges
    {
        core::perf::ScopedTimer perf_scope_nodes("gsn.nodes");
        for (const auto& node : layout_nodes_) {
            ImVec2 node_min(origin.x + node.position.x * zoom, origin.y + node.position.y * zoom);
            ImVec2 node_max(node_min.x + node.size.x * zoom, node_min.y + node.size.y * zoom);
            if (!RectsIntersect(node_min, node_max, cull_min, cull_max)) {
                ++frame_stats.nodes_culled;
                continue;
            }

            GsnNode gsn_node;
            gsn_node.id = node.id;
            switch (node.role) {
            case ElementRole::Claim:
                gsn_node.type = "Claim";
                break;
            case ElementRole::Strategy:
                gsn_node.type = "Strategy";
                break;
            case ElementRole::Solution:
                gsn_node.type = "Solution";
                break;
            case ElementRole::Context:
                gsn_node.type = "Context";
                break;
            case ElementRole::Assumption:
                gsn_node.type = "Assumption";
                break;
            case ElementRole::Justification:
                gsn_node.type = "Justification";
                break;
            case ElementRole::Evidence:
                gsn_node.type = "Evidence";
                break;
            default:
                gsn_node.type = "Other";
                break;
            }
            gsn_node.position = node.position;
            gsn_node.size = node.size;
            gsn_node.label = node.label;
            gsn_node.label_secondary = node.label_secondary;
            gsn_node.undeveloped = node.undeveloped;
            {
                core::perf::ScopedTimer perf_scope("gsn.node.draw");
                DrawGsnNode(gsn_node,
                            origin,
                            ui_state,
                            active_case,
                            actions,
                            terminology_service_ptr,
                            terminology_package,
                            &terminology_card_state_,
                            zoom,
                            overlay_hovered);
            }
            const auto element_acps = acp_by_element.find(node.id);
            if (element_acps != acp_by_element.end() && core::perf::GetPerfToggles().acp_decorators) {
                core::perf::ScopedTimer perf_scope("gsn.node.acp_decorator");
                DrawAcpElementDecorator(
                    draw_list, node, node_min, node_max, zoom, element_acps->second, actions, ui_state);
                ++frame_stats.acp_decorators_drawn;
            }
            ++frame_stats.nodes_drawn;
        }
    } // gsn.nodes

    RenderPinnedTerminologyCard(terminology_card_state_, terminology_package, actions);
    if (terminology_card_state_.pinned && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !terminology_card_state_.clicked_term_this_frame && !terminology_card_state_.card_hovered_this_frame &&
        !ImGui::IsAnyItemHovered()) {
        terminology_card_state_.pinned = false;
    }

    frame_stats.draw_list_vtx = draw_list->VtxBuffer.Size;
    frame_stats.draw_list_idx = draw_list->IdxBuffer.Size;
    frame_stats.draw_list_cmds = draw_list->CmdBuffer.Size;
    last_render_stats_ = frame_stats;
    g_last_render_stats_snapshot = frame_stats;
    SetCurrentRenderStats(nullptr);
}

bool GsnCanvas::CenterOnNode(const std::string& node_id, ImVec2 viewport_size) {
    for (const auto& node : layout_nodes_) {
        if (node.id == node_id) {
            // Center of the node in layout-space (unzoomed)
            float cx = node.position.x + node.size.x * 0.5f;
            float cy = node.position.y + node.size.y * 0.5f;
            // Set view_offset so the node center maps to viewport center
            view_offset_.x = cx * zoom_level_ - viewport_size.x * 0.5f;
            view_offset_.y = cy * zoom_level_ - viewport_size.y * 0.5f;
            return true;
        }
    }
    return false;
}

bool GsnCanvas::CenterOnIds(const std::unordered_set<std::string>& ids, ImVec2 viewport_size) {
    if (ids.empty())
        return false;

    bool found_any = false;
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    for (const auto& node : layout_nodes_) {
        if (!ids.count(node.id))
            continue;
        const float nx0 = node.position.x;
        const float ny0 = node.position.y;
        const float nx1 = nx0 + node.size.x;
        const float ny1 = ny0 + node.size.y;
        if (!found_any) {
            min_x = nx0;
            min_y = ny0;
            max_x = nx1;
            max_y = ny1;
            found_any = true;
        } else {
            if (nx0 < min_x)
                min_x = nx0;
            if (ny0 < min_y)
                min_y = ny0;
            if (nx1 > max_x)
                max_x = nx1;
            if (ny1 > max_y)
                max_y = ny1;
        }
    }
    if (!found_any)
        return false;

    // Pad the AABB so the marked nodes don't sit flush with the viewport edge.
    const float padding = DpiSize(80.0f); // layout-space pixels
    const float aabb_w = std::max(1.0f, (max_x - min_x) + padding * 2.0f);
    const float aabb_h = std::max(1.0f, (max_y - min_y) + padding * 2.0f);

    // Compute zoom that fits AABB in viewport, then clamp.
    const float zoom_x = viewport_size.x / aabb_w;
    const float zoom_y = viewport_size.y / aabb_h;
    float new_zoom = std::min(zoom_x, zoom_y);
    // Clamp to a sensible range; do not zoom further IN than 1.0 (no need).
    if (new_zoom > 1.0f)
        new_zoom = 1.0f;
    if (new_zoom < 0.1f)
        new_zoom = 0.1f;
    zoom_level_ = new_zoom;

    const float cx = (min_x + max_x) * 0.5f;
    const float cy = (min_y + max_y) * 0.5f;
    view_offset_.x = cx * zoom_level_ - viewport_size.x * 0.5f;
    view_offset_.y = cy * zoom_level_ - viewport_size.y * 0.5f;
    return true;
}

} // namespace ui::gsn
