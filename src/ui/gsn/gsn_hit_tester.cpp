#include "ui/gsn/gsn_hit_tester.h"

#include "ui/gsn/gsn_dpi.h"
#include "ui/gsn/gsn_edge_renderer.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace ui::gsn {

namespace {

float DistanceSq(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float DistanceSqPointToSegment(ImVec2 point, ImVec2 start, ImVec2 end) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length_sq = dx * dx + dy * dy;
    if (length_sq < 1e-6f)
        return DistanceSq(point, start);

    const float t = std::max(0.0f, std::min(1.0f, ((point.x - start.x) * dx + (point.y - start.y) * dy) / length_sq));
    const ImVec2 closest(start.x + dx * t, start.y + dy * t);
    return DistanceSq(point, closest);
}

float DistanceSqPointToBezier(ImVec2 point, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3) {
    float best = FLT_MAX;
    ImVec2 previous = p0;
    for (int i = 1; i <= kBezierSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kBezierSamples);
        const ImVec2 current = EvalBezier(p0, p1, p2, p3, t);
        best = std::min(best, DistanceSqPointToSegment(point, previous, current));
        previous = current;
    }
    return best;
}

float DistanceSqToGroup1Edge(ImVec2 point, ImVec2 parent_bottom, ImVec2 child_top, float zoom) {
    const float scale = DpiScale() * zoom;
    const float scaled_stub = kStubLength * scale;
    const ImVec2 stub_start(parent_bottom.x, parent_bottom.y + scaled_stub);
    const ImVec2 stub_end(child_top.x, child_top.y - scaled_stub);
    const float vertical_span = fabsf(stub_end.y - stub_start.y);
    const ImVec2 ctrl_1(stub_start.x, stub_start.y + vertical_span * kVerticalControlPct);
    const ImVec2 ctrl_2(stub_end.x, stub_end.y - vertical_span * kVerticalControlPct);

    float best = DistanceSqPointToSegment(point, parent_bottom, stub_start);
    best = std::min(best, DistanceSqPointToBezier(point, stub_start, ctrl_1, ctrl_2, stub_end));
    best = std::min(best, DistanceSqPointToSegment(point, stub_end, child_top));
    return best;
}

float DistanceSqToGroup2Edge(ImVec2 point, ImVec2 parent_side, ImVec2 attachment_edge, bool is_left_side, float zoom) {
    const float horizontal_sign = is_left_side ? -1.0f : 1.0f;
    const float scale = DpiScale() * zoom;
    const float scaled_stub = kStubLength * scale;
    const ImVec2 stub_start(parent_side.x + horizontal_sign * scaled_stub, parent_side.y);
    const ImVec2 stub_end(attachment_edge.x - horizontal_sign * scaled_stub, attachment_edge.y);
    const float horizontal_span = fabsf(stub_end.x - stub_start.x) * 0.5f;
    const ImVec2 ctrl_1(stub_start.x + horizontal_sign * horizontal_span, stub_start.y);
    const ImVec2 ctrl_2(stub_end.x - horizontal_sign * horizontal_span, stub_end.y);

    float best = DistanceSqPointToSegment(point, parent_side, stub_start);
    best = std::min(best, DistanceSqPointToBezier(point, stub_start, ctrl_1, ctrl_2, stub_end));
    best = std::min(best, DistanceSqPointToSegment(point, stub_end, attachment_edge));
    return best;
}

} // namespace

std::string EdgeKey(const std::string& parent_id, const std::string& child_id) {
    return parent_id + "\x1f" + child_id;
}

bool RectsIntersect(ImVec2 a_min, ImVec2 a_max, ImVec2 b_min, ImVec2 b_max) {
    return a_max.x >= b_min.x && a_min.x <= b_max.x && a_max.y >= b_min.y && a_min.y <= b_max.y;
}

bool RelationshipEdgeSelected(const UiState& ui_state,
                              const core::acp::AcpRelationshipTarget* target,
                              const std::string& edge_key) {
    return target && ui_state.selected_relationship_id == target->relationship_id &&
           ui_state.selected_relationship_edge_key == edge_key;
}

bool PointInsideNode(ImVec2 point, const LayoutNode& node, ImVec2 origin, float zoom) {
    const ImVec2 node_min(origin.x + node.position.x * zoom, origin.y + node.position.y * zoom);
    const ImVec2 node_max(node_min.x + node.size.x * zoom, node_min.y + node.size.y * zoom);
    return point.x >= node_min.x && point.x <= node_max.x && point.y >= node_min.y && point.y <= node_max.y;
}

std::string PickRelationshipEdge(const std::vector<LayoutNode>& layout_nodes,
                                 const std::unordered_map<std::string, const LayoutNode*>& node_by_id,
                                 ImVec2 origin,
                                 float zoom,
                                 ImVec2 cull_min,
                                 ImVec2 cull_max) {
    const bool can_pick =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_ChildWindows);
    if (!can_pick)
        return {};

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mouse.x < cull_min.x || mouse.x > cull_max.x || mouse.y < cull_min.y || mouse.y > cull_max.y)
        return {};

    for (const LayoutNode& node : layout_nodes) {
        if (PointInsideNode(mouse, node, origin, zoom))
            return {};
    }

    const float tolerance = DpiSize(10.0f);
    const float tolerance_sq = tolerance * tolerance;
    float best_distance_sq = tolerance_sq;
    std::string best_edge_key;

    for (const LayoutNode& child_node : layout_nodes) {
        if (child_node.parent_id.empty())
            continue;
        const auto parent_it = node_by_id.find(child_node.parent_id);
        if (parent_it == node_by_id.end())
            continue;
        const LayoutNode& parent_node = *parent_it->second;

        float distance_sq = FLT_MAX;
        if (child_node.group == ElementGroup::Group2) {
            ImVec2 parent_side, attachment_edge;
            ComputeGroup2Endpoints(parent_node, child_node, origin, zoom, parent_side, attachment_edge);
            ImVec2 edge_min, edge_max;
            ComputeGroup2EdgeBounds(parent_side, attachment_edge, child_node.is_left_side, zoom, edge_min, edge_max);
            edge_min.x -= tolerance;
            edge_min.y -= tolerance;
            edge_max.x += tolerance;
            edge_max.y += tolerance;
            if (!RectsIntersect(edge_min, edge_max, cull_min, cull_max) || mouse.x < edge_min.x ||
                mouse.x > edge_max.x || mouse.y < edge_min.y || mouse.y > edge_max.y) {
                continue;
            }
            distance_sq = DistanceSqToGroup2Edge(mouse, parent_side, attachment_edge, child_node.is_left_side, zoom);
        } else {
            ImVec2 parent_bottom, child_top;
            ComputeGroup1Endpoints(parent_node, child_node, origin, zoom, parent_bottom, child_top);
            ImVec2 edge_min, edge_max;
            ComputeGroup1EdgeBounds(parent_bottom, child_top, zoom, edge_min, edge_max);
            edge_min.x -= tolerance;
            edge_min.y -= tolerance;
            edge_max.x += tolerance;
            edge_max.y += tolerance;
            if (!RectsIntersect(edge_min, edge_max, cull_min, cull_max) || mouse.x < edge_min.x ||
                mouse.x > edge_max.x || mouse.y < edge_min.y || mouse.y > edge_max.y) {
                continue;
            }
            distance_sq = DistanceSqToGroup1Edge(mouse, parent_bottom, child_top, zoom);
        }

        if (distance_sq <= best_distance_sq) {
            best_distance_sq = distance_sq;
            best_edge_key = EdgeKey(parent_node.id, child_node.id);
        }
    }

    return best_edge_key;
}

} // namespace ui::gsn
