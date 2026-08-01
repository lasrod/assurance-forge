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
#include "ui/i18n/localization.h"
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

// Squared distance from point `p` to segment [a,b]. Used to hit-test the
// dashed challenge edges (which are not part of the structural edge picker).
static float DistanceSqPointSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float wx = p.x - a.x;
    const float wy = p.y - a.y;
    const float c1 = vx * wx + vy * wy;
    if (c1 <= 0.0f)
        return wx * wx + wy * wy;
    const float c2 = vx * vx + vy * vy;
    if (c2 <= c1) {
        const float dx = p.x - b.x;
        const float dy = p.y - b.y;
        return dx * dx + dy * dy;
    }
    const float t = c1 / c2;
    const float px = a.x + t * vx;
    const float py = a.y + t * vy;
    return (p.x - px) * (p.x - px) + (p.y - py) * (p.y - py);
}

// Point on the border of rectangle [mn,mx] along the ray from its center toward
// `toward`. Used to anchor challenge edges to a node's edge rather than center.
static ImVec2 RectBorderToward(ImVec2 mn, ImVec2 mx, ImVec2 toward) {
    const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float dx = toward.x - c.x;
    const float dy = toward.y - c.y;
    if (std::fabs(dx) < 1e-3f && std::fabs(dy) < 1e-3f)
        return c;
    const float hx = (mx.x - mn.x) * 0.5f;
    const float hy = (mx.y - mn.y) * 0.5f;
    const float tx = (std::fabs(dx) > 1e-3f) ? hx / std::fabs(dx) : FLT_MAX;
    const float ty = (std::fabs(dy) > 1e-3f) ? hy / std::fabs(dy) : FLT_MAX;
    const float t = std::min(tx, ty);
    return ImVec2(c.x + dx * t, c.y + dy * t);
}

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

void GsnCanvas::SetTree(const core::AssuranceTree& tree, const std::string& anchor_node_id) {
    // Capture the anchor node's center in layout space before the recompute
    // so we can shift the view to keep it visually fixed afterwards.
    std::optional<ImVec2> anchor_before;
    if (!anchor_node_id.empty()) {
        auto it = node_by_id_.find(anchor_node_id);
        if (it != node_by_id_.end() && it->second != nullptr) {
            const LayoutNode& n = *it->second;
            anchor_before = ImVec2(n.position.x + n.size.x * 0.5f, n.position.y + n.size.y * 0.5f);
        }
    }

    LayoutEngine le;
    layout_nodes_ = le.ComputeLayout(tree);
    RebuildNodeLookup();

    if (anchor_before.has_value()) {
        auto it = node_by_id_.find(anchor_node_id);
        if (it != node_by_id_.end() && it->second != nullptr) {
            const LayoutNode& n = *it->second;
            ImVec2 anchor_after(n.position.x + n.size.x * 0.5f, n.position.y + n.size.y * 0.5f);
            // Keep `screen = canvas_origin - view_offset_ + pos*zoom` constant
            // for the anchor by absorbing the layout-space delta into view_offset_.
            view_offset_.x += (anchor_after.x - anchor_before->x) * zoom_level_;
            view_offset_.y += (anchor_after.y - anchor_before->y) * zoom_level_;
        }
    }
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

    if (active_case == nullptr) {
        cached_acp_targets_case_ = nullptr;
        cached_acp_targets_revision_ = ~std::uint64_t{0};
        cached_acp_targets_.clear();
    } else if (active_case != cached_acp_targets_case_ || case_revision_ != cached_acp_targets_revision_) {
        core::perf::ScopedTimer perf_scope_acp("gsn.acp.build_targets");
        cached_acp_targets_ = core::acp::BuildAcpRelationshipTargets(*active_case);
        cached_acp_targets_case_ = active_case;
        cached_acp_targets_revision_ = case_revision_;
    }
    const std::vector<core::acp::AcpRelationshipTarget>& acp_targets = cached_acp_targets_;
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

    // First pass: record the screen-space midpoint of every structural/contextual
    // relationship edge, keyed by relationship id. A dialectic challenge that
    // targets a relationship anchors its arrow at this midpoint (the same point
    // the ACP decorator uses), rather than at an element body.
    std::unordered_map<std::string, ImVec2> relationship_midpoint;
    std::unordered_map<std::string, bool> relationship_midpoint_primary; // chosen edge is the primary one
    for (const auto& child_node : layout_nodes_) {
        if (child_node.parent_id.empty() || child_node.is_counter_source)
            continue;
        auto parent_it = node_by_id_.find(child_node.parent_id);
        if (parent_it == node_by_id_.end())
            continue;
        const LayoutNode& parent_node = *parent_it->second;
        const std::string edge_key = EdgeKey(parent_node.id, child_node.id);
        const auto target_it = acp_target_by_edge.find(edge_key);
        if (target_it == acp_target_by_edge.end() || !target_it->second)
            continue;
        ImVec2 a, b;
        if (child_node.group == ElementGroup::Group2)
            ComputeGroup2Endpoints(parent_node, child_node, origin, zoom, a, b);
        else
            ComputeGroup1Endpoints(parent_node, child_node, origin, zoom, a, b);
        // A relationship rendered as several edges (e.g. an inference with a
        // reasoning strategy: parent->strategy plus strategy->child) maps every
        // edge to the SAME relationship id. Anchor a relationship-targeted
        // challenge deterministically: keep the first edge seen, but let the
        // primary edge (the labelled relationship edge, not a strategy-child edge)
        // win so the arrow lands on a stable point regardless of iteration order.
        const std::string& rel_id = target_it->second->relationship_id;
        const bool primary = !target_it->second->strategy_child_edge;
        const bool seen = relationship_midpoint.count(rel_id) != 0;
        if (!seen || (primary && !relationship_midpoint_primary[rel_id])) {
            relationship_midpoint[rel_id] = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            relationship_midpoint_primary[rel_id] = primary;
        }
    }

    // Draw edges first (beneath nodes)
    {
        core::perf::ScopedTimer perf_scope_edges("gsn.edges");
        for (const auto& child_node : layout_nodes_) {
            // Counters are cluster roots with no structural parent; their dashed
            // challenge edge is drawn by the dedicated pass below, not here.
            if (child_node.parent_id.empty() || child_node.is_counter_source)
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
                const float straight_drop = static_cast<float>(parent_node.child_edge_drop) * zoom;
                ImVec2 edge_min, edge_max;
                ComputeGroup1EdgeBounds(parent_bottom, child_top, zoom, edge_min, edge_max, straight_drop);
                if (!RectsIntersect(edge_min, edge_max, cull_min, cull_max)) {
                    ++frame_stats.edges_culled;
                    continue;
                }
                DrawGroup1Edge(draw_list, parent_bottom, child_top, zoom, straight_drop);
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
                    DrawGroup1EdgeHighlight(draw_list, parent_bottom, child_top, zoom, straight_drop);
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

    // Dialectic challenge edges: a dashed open-arrow from each counter element's
    // border to its target (an element border, or a relationship midpoint). Drawn
    // after support/contextual edges so the relationship midpoints are known, but
    // still beneath the nodes.
    {
        core::perf::ScopedTimer perf_scope_challenge("gsn.challenge_edges");
        for (const auto& counter : layout_nodes_) {
            if (!counter.is_counter_source)
                continue;
            ImVec2 c_min(origin.x + counter.position.x * zoom, origin.y + counter.position.y * zoom);
            ImVec2 c_max(c_min.x + counter.size.x * zoom, c_min.y + counter.size.y * zoom);
            const ImVec2 c_center((c_min.x + c_max.x) * 0.5f, (c_min.y + c_max.y) * 0.5f);

            ImVec2 to;
            bool have_to = false;
            if (counter.challenge_target_is_relationship) {
                const auto mid_it = relationship_midpoint.find(counter.challenge_target_id);
                if (mid_it != relationship_midpoint.end()) {
                    to = mid_it->second;
                    have_to = true;
                }
            } else {
                const auto it = node_by_id_.find(counter.challenge_target_id);
                if (it != node_by_id_.end() && it->second) {
                    const LayoutNode& t = *it->second;
                    ImVec2 t_min(origin.x + t.position.x * zoom, origin.y + t.position.y * zoom);
                    ImVec2 t_max(t_min.x + t.size.x * zoom, t_min.y + t.size.y * zoom);
                    to = RectBorderToward(t_min, t_max, c_center);
                    have_to = true;
                }
            }
            if (!have_to)
                continue;

            const ImVec2 from = RectBorderToward(c_min, c_max, to);

            // Record the arrow midpoint so a challenge-of-a-challenge can anchor.
            if (!counter.challenge_relationship_id.empty())
                relationship_midpoint[counter.challenge_relationship_id] =
                    ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f);

            ImVec2 edge_min, edge_max;
            ComputeChallengeEdgeBounds(from, to, zoom, edge_min, edge_max);
            if (!RectsIntersect(edge_min, edge_max, cull_min, cull_max)) {
                ++frame_stats.edges_culled;
                continue;
            }

            // The challenge arrow is itself a relationship that can be challenged:
            // hit-test it and offer the counter actions on right-click.
            const float pick = DpiSize(7.0f);
            const bool hovered =
                !overlay_hovered && DistanceSqPointSegment(ImGui::GetIO().MousePos, from, to) <= pick * pick;
            if (hovered)
                DrawChallengeEdgeHighlight(draw_list, from, to, zoom);
            DrawChallengeEdge(draw_list, from, to, zoom);

            const std::string popup_id = "gsn_challenge_edge##" + counter.challenge_relationship_id;
            if (hovered && !counter.challenge_relationship_id.empty() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup(popup_id.c_str());
            }
            if (ImGui::BeginPopup(popup_id.c_str())) {
                frame_stats.relationship_context_menu_active = true;
                ImGui::TextUnformatted(AF_TR("Challenge").c_str());
                ImGui::Separator();
                if (ImGui::MenuItem(AF_TR("Add Counter Argument").c_str(),
                                    nullptr,
                                    false,
                                    static_cast<bool>(actions.add_counter_argument_to_relationship)))
                    actions.add_counter_argument_to_relationship(counter.challenge_relationship_id);
                if (ImGui::MenuItem(AF_TR("Add Counter Evidence").c_str(),
                                    nullptr,
                                    false,
                                    static_cast<bool>(actions.add_counter_evidence_to_relationship)))
                    actions.add_counter_evidence_to_relationship(counter.challenge_relationship_id);
                ImGui::EndPopup();
            }
            ++frame_stats.edges_drawn;
        }
    } // gsn.challenge_edges

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
            gsn_node.uninstantiated = node.uninstantiated;
            gsn_node.is_counter = node.is_counter_source;
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
                            overlay_hovered,
                            &terminology_occurrence_cache_);
            }
            const auto element_acps = acp_by_element.find(node.id);
            if (element_acps != acp_by_element.end() && core::perf::GetPerfToggles().acp_decorators) {
                core::perf::ScopedTimer perf_scope("gsn.node.acp_decorator");
                DrawAcpElementDecorator(
                    draw_list, node, node_min, node_max, zoom, element_acps->second, actions, ui_state);
                ++frame_stats.acp_decorators_drawn;
            }
            // History-timeline highlight overlay (read-only audit view).
            if (!history_highlights_.empty()) {
                auto hl_it = history_highlights_.find(node.id);
                if (hl_it != history_highlights_.end()) {
                    ImU32 color = 0;
                    switch (hl_it->second) {
                    case core::audit::HistoryHighlightKind::Added:
                        color = ui::GetTheme().success;
                        break;
                    case core::audit::HistoryHighlightKind::Modified:
                        color = ui::GetTheme().warning;
                        break;
                    case core::audit::HistoryHighlightKind::Deleted:
                        color = ui::GetTheme().attention;
                        break;
                    }
                    const float thickness = std::max(2.0f, 3.0f * zoom);
                    const float pad = thickness * 0.5f + 1.0f;
                    draw_list->AddRect(ImVec2(node_min.x - pad, node_min.y - pad),
                                       ImVec2(node_max.x + pad, node_max.y + pad),
                                       color,
                                       4.0f * zoom,
                                       0,
                                       thickness);
                }
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

bool GsnCanvas::ConsumePendingFocus(ImVec2 viewport_size) {
    if (!has_pending_focus_)
        return false;
    bool applied = false;
    if (!pending_focus_ids_.empty())
        applied = CenterOnIds(pending_focus_ids_, viewport_size);
    if (!applied && pending_focus_fit_all_fallback_) {
        // Build a set of every node id currently in the layout and fit to
        // that. Cheaper than adding a separate "fit all" code path.
        std::unordered_set<std::string> all_ids;
        all_ids.reserve(layout_nodes_.size());
        for (const auto& node : layout_nodes_)
            all_ids.insert(node.id);
        if (!all_ids.empty())
            applied = CenterOnIds(all_ids, viewport_size);
    }
    pending_focus_ids_.clear();
    pending_focus_fit_all_fallback_ = false;
    has_pending_focus_ = false;
    return applied;
}

} // namespace ui::gsn
