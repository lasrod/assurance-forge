#include "export/gsn_layout.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace export_gsn {
namespace {

constexpr double kMargin = 48.0;
constexpr double kColumnWidth = 320.0;
constexpr double kRowHeight = 170.0;
constexpr double kSideGap = 32.0;
constexpr double kSideStackGap = 18.0;

bool IsSideInformation(GsnNodeKind kind) {
    return kind == GsnNodeKind::Context || kind == GsnNodeKind::Assumption || kind == GsnNodeKind::Justification;
}

void ApplyNodeSize(GsnNode& node) {
    switch (node.kind) {
    case GsnNodeKind::Goal:
        node.width = 240.0;
        node.height = 86.0;
        break;
    case GsnNodeKind::Strategy:
        node.width = 260.0;
        node.height = 86.0;
        break;
    case GsnNodeKind::Solution:
        node.width = 116.0;
        node.height = 116.0;
        break;
    case GsnNodeKind::Context:
        node.width = 210.0;
        node.height = 76.0;
        break;
    case GsnNodeKind::Assumption:
    case GsnNodeKind::Justification:
        node.width = 190.0;
        node.height = 82.0;
        break;
    }
}

struct LayoutState {
    GsnDiagram& diagram;
    std::vector<std::string> warnings;
    std::unordered_map<std::string, size_t> node_by_id;
    std::unordered_map<std::string, std::vector<std::string>> support_children;
    std::unordered_map<std::string, std::vector<std::string>> side_children;
    std::unordered_set<std::string> support_child_ids;
    std::unordered_set<std::string> side_child_ids;
    std::unordered_map<std::string, int> spans;
    std::unordered_map<std::string, int> visit_state;
    std::unordered_map<std::string, int> placement_state;
    bool cycle_seen = false;
};

int ComputeSpan(LayoutState& state, const std::string& node_id) {
    int& visit = state.visit_state[node_id];
    if (visit == 1) {
        state.cycle_seen = true;
        return 1;
    }
    if (visit == 2)
        return state.spans[node_id];

    visit = 1;
    int span = 0;
    for (const std::string& child_id : state.support_children[node_id]) {
        if (state.node_by_id.find(child_id) == state.node_by_id.end())
            continue;
        span += ComputeSpan(state, child_id);
    }
    if (span <= 0)
        span = 1;
    state.spans[node_id] = span;
    visit = 2;
    return span;
}

void PlaceSideChildren(LayoutState& state, const std::string& node_id) {
    auto parent_it = state.node_by_id.find(node_id);
    if (parent_it == state.node_by_id.end())
        return;
    GsnNode& parent = state.diagram.nodes[parent_it->second];
    const auto side_it = state.side_children.find(node_id);
    if (side_it == state.side_children.end())
        return;

    int left_count = 0;
    int right_count = 0;
    for (size_t i = 0; i < side_it->second.size(); ++i) {
        const std::string& child_id = side_it->second[i];
        auto child_it = state.node_by_id.find(child_id);
        if (child_it == state.node_by_id.end())
            continue;
        GsnNode& child = state.diagram.nodes[child_it->second];
        const bool place_left = (i % 2 == 0);
        int& stack_index = place_left ? left_count : right_count;
        const double stack_y = parent.y + stack_index * (child.height + kSideStackGap);
        if (place_left) {
            child.x = parent.x - kSideGap - child.width;
        } else {
            child.x = parent.x + parent.width + kSideGap;
        }
        child.y = stack_y;
        ++stack_index;
    }
}

void PlaceSubtree(LayoutState& state, const std::string& node_id, int left_column, int depth) {
    int& placement = state.placement_state[node_id];
    if (placement == 1) {
        state.cycle_seen = true;
        return;
    }
    if (placement == 2)
        return;
    placement = 1;

    auto node_it = state.node_by_id.find(node_id);
    if (node_it == state.node_by_id.end()) {
        placement = 2;
        return;
    }

    GsnNode& node = state.diagram.nodes[node_it->second];
    const int span = std::max(1, state.spans[node_id]);
    const double center_x = kMargin + (static_cast<double>(left_column) + static_cast<double>(span) / 2.0) * kColumnWidth;
    node.x = center_x - node.width / 2.0;
    node.y = kMargin + static_cast<double>(depth) * kRowHeight;

    PlaceSideChildren(state, node_id);

    int child_left = left_column;
    for (const std::string& child_id : state.support_children[node_id]) {
        if (state.node_by_id.find(child_id) == state.node_by_id.end())
            continue;
        const int child_span = std::max(1, state.spans[child_id]);
        PlaceSubtree(state, child_id, child_left, depth + 1);
        child_left += child_span;
    }
    placement = 2;
}

void ShiftIntoPositiveCoordinates(GsnDiagram& diagram) {
    if (diagram.nodes.empty())
        return;

    double min_x = diagram.nodes.front().x;
    double min_y = diagram.nodes.front().y;
    for (const GsnNode& node : diagram.nodes) {
        min_x = std::min(min_x, node.x);
        min_y = std::min(min_y, node.y);
    }

    const double dx = min_x < kMargin ? kMargin - min_x : 0.0;
    const double dy = min_y < kMargin ? kMargin - min_y : 0.0;
    if (dx == 0.0 && dy == 0.0)
        return;

    for (GsnNode& node : diagram.nodes) {
        node.x += dx;
        node.y += dy;
    }
}

} // namespace

GsnLayoutResult LayoutGsnDiagram(GsnDiagram& diagram) {
    GsnLayoutResult result;
    LayoutState state{diagram};

    for (size_t i = 0; i < diagram.nodes.size(); ++i) {
        ApplyNodeSize(diagram.nodes[i]);
        state.node_by_id[diagram.nodes[i].id] = i;
    }

    for (const GsnEdge& edge : diagram.edges) {
        if (state.node_by_id.find(edge.from_id) == state.node_by_id.end() ||
            state.node_by_id.find(edge.to_id) == state.node_by_id.end()) {
            result.warnings.push_back("Layout skipped edge '" + edge.id + "' because an endpoint was missing.");
            continue;
        }
        if (edge.kind == GsnEdgeKind::InContextOf) {
            state.side_children[edge.from_id].push_back(edge.to_id);
            state.side_child_ids.insert(edge.to_id);
        } else {
            state.support_children[edge.from_id].push_back(edge.to_id);
            state.support_child_ids.insert(edge.to_id);
        }
    }

    std::vector<std::string> roots;
    for (const GsnNode& node : diagram.nodes) {
        if (state.support_child_ids.find(node.id) == state.support_child_ids.end() &&
            state.side_child_ids.find(node.id) == state.side_child_ids.end() && !IsSideInformation(node.kind)) {
            roots.push_back(node.id);
        }
    }
    if (roots.empty() && !diagram.nodes.empty()) {
        roots.push_back(diagram.nodes.front().id);
        result.warnings.push_back("Layout fallback was used because no root goal could be determined.");
    }

    for (const std::string& root_id : roots) {
        ComputeSpan(state, root_id);
    }
    if (state.cycle_seen) {
        result.warnings.push_back("Layout detected a cycle and used a best-effort traversal.");
    }

    int left_column = 0;
    for (const std::string& root_id : roots) {
        PlaceSubtree(state, root_id, left_column, 0);
        left_column += std::max(1, state.spans[root_id]) + 1;
    }

    int orphan_column = 0;
    std::unordered_set<std::string> placed;
    for (const GsnNode& node : diagram.nodes) {
        if (node.width > 0.0 && node.height > 0.0 && (node.x != 0.0 || node.y != 0.0))
            placed.insert(node.id);
    }
    for (GsnNode& node : diagram.nodes) {
        if (placed.find(node.id) != placed.end())
            continue;
        node.x = kMargin + static_cast<double>(orphan_column) * kColumnWidth;
        node.y = kMargin + static_cast<double>(roots.empty() ? 0 : 2) * kRowHeight;
        ++orphan_column;
        result.warnings.push_back("Layout placed orphan node '" + node.id + "' using fallback positioning.");
    }

    ShiftIntoPositiveCoordinates(diagram);
    return result;
}

} // namespace export_gsn
