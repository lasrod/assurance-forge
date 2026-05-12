#include "export/gsn_layout.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace export_gsn {
namespace {

constexpr double kMargin = 48.0;
constexpr double kColumnWidth = 360.0;
constexpr double kRowHeight = 170.0;
constexpr double kSideGap = 36.0;
constexpr double kSideStackGap = 20.0;
constexpr double kSupportGap = 88.0;
constexpr double kTextLineHeight = 18.0;
constexpr double kTextCharWidth = 7.0;
constexpr double kTextVerticalPadding = 38.0;

bool IsSideInformation(GsnNodeKind kind) {
    return kind == GsnNodeKind::Context || kind == GsnNodeKind::Assumption || kind == GsnNodeKind::Justification;
}

struct NodeSizeLimits {
    double base_width = 240.0;
    double base_height = 86.0;
    double max_width = 360.0;
};

NodeSizeLimits SizeLimitsFor(GsnNodeKind kind) {
    switch (kind) {
    case GsnNodeKind::Goal:
        return {240.0, 86.0, 380.0};
    case GsnNodeKind::Strategy:
        return {260.0, 86.0, 400.0};
    case GsnNodeKind::Solution:
        return {116.0, 116.0, 220.0};
    case GsnNodeKind::Context:
        return {210.0, 76.0, 360.0};
    case GsnNodeKind::Assumption:
    case GsnNodeKind::Justification:
        return {190.0, 82.0, 340.0};
    }
    return {};
}

size_t WrappedLineCount(const GsnNode& node, double width) {
    const double available_width = node.kind == GsnNodeKind::Solution ? width * 0.62 : width - 36.0;
    const size_t max_chars = std::max<size_t>(8, static_cast<size_t>(available_width / kTextCharWidth));
    size_t lines = 1;

    std::istringstream paragraphs(node.text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        std::istringstream words(paragraph);
        std::string word;
        size_t line_chars = 0;
        bool has_word = false;
        while (words >> word) {
            has_word = true;
            if (line_chars == 0) {
                line_chars = word.size();
            } else if (line_chars + 1 + word.size() <= max_chars) {
                line_chars += 1 + word.size();
            } else {
                ++lines;
                line_chars = word.size();
            }
            while (line_chars > max_chars && max_chars > 4) {
                ++lines;
                line_chars -= max_chars;
            }
        }
        if (has_word)
            ++lines;
    }

    return lines;
}

void ApplyNodeSize(GsnNode& node) {
    const NodeSizeLimits limits = SizeLimitsFor(node.kind);
    double width = limits.base_width;
    size_t line_count = WrappedLineCount(node, width);
    while (width < limits.max_width && line_count > 5) {
        width = std::min(limits.max_width, width + 30.0);
        line_count = WrappedLineCount(node, width);
    }

    const double required_height = static_cast<double>(line_count) * kTextLineHeight + kTextVerticalPadding;
    if (node.kind == GsnNodeKind::Solution) {
        const double diameter = std::max({limits.base_width, width, required_height / 0.72});
        node.width = diameter;
        node.height = diameter;
    } else {
        node.width = width;
        node.height = std::max(limits.base_height, required_height);
    }
}

struct LayoutState {
    GsnDiagram& diagram;
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

double StackHeight(const std::vector<GsnNode*>& nodes) {
    if (nodes.empty())
        return 0.0;
    double height = 0.0;
    for (const GsnNode* node : nodes)
        height += node->height;
    height += static_cast<double>(nodes.size() - 1) * kSideStackGap;
    return height;
}

void SideStacks(LayoutState& state,
                const std::string& node_id,
                std::vector<GsnNode*>& left_nodes,
                std::vector<GsnNode*>& right_nodes) {
    const auto side_it = state.side_children.find(node_id);
    if (side_it == state.side_children.end())
        return;

    for (size_t i = 0; i < side_it->second.size(); ++i) {
        auto child_it = state.node_by_id.find(side_it->second[i]);
        if (child_it == state.node_by_id.end())
            continue;
        if (i % 2 == 0)
            left_nodes.push_back(&state.diagram.nodes[child_it->second]);
        else
            right_nodes.push_back(&state.diagram.nodes[child_it->second]);
    }
}

double SideStackHeight(LayoutState& state, const std::string& node_id) {
    std::vector<GsnNode*> left_nodes;
    std::vector<GsnNode*> right_nodes;
    SideStacks(state, node_id, left_nodes, right_nodes);
    return std::max(StackHeight(left_nodes), StackHeight(right_nodes));
}

void PlaceSideChildren(LayoutState& state, const std::string& node_id) {
    auto parent_it = state.node_by_id.find(node_id);
    if (parent_it == state.node_by_id.end())
        return;

    GsnNode& parent = state.diagram.nodes[parent_it->second];
    std::vector<GsnNode*> left_nodes;
    std::vector<GsnNode*> right_nodes;
    SideStacks(state, node_id, left_nodes, right_nodes);

    auto place_stack = [&](const std::vector<GsnNode*>& nodes, bool left_side) {
        double y = parent.y + parent.height / 2.0 - StackHeight(nodes) / 2.0;
        for (GsnNode* child : nodes) {
            child->x = left_side ? parent.x - kSideGap - child->width : parent.x + parent.width + kSideGap;
            child->y = y;
            y += child->height + kSideStackGap;
        }
    };

    place_stack(left_nodes, true);
    place_stack(right_nodes, false);
}

double SupportYOffset(LayoutState& state, const std::string& node_id) {
    auto node_it = state.node_by_id.find(node_id);
    if (node_it == state.node_by_id.end())
        return kRowHeight;
    const GsnNode& node = state.diagram.nodes[node_it->second];
    return std::max(kRowHeight, std::max(node.height, SideStackHeight(state, node_id)) + kSupportGap);
}

void PlaceSubtree(LayoutState& state, const std::string& node_id, int left_column, double y) {
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
    node.y = y;

    PlaceSideChildren(state, node_id);

    int child_left = left_column;
    const double child_y = y + SupportYOffset(state, node_id);
    for (const std::string& child_id : state.support_children[node_id]) {
        if (state.node_by_id.find(child_id) == state.node_by_id.end())
            continue;
        const int child_span = std::max(1, state.spans[child_id]);
        PlaceSubtree(state, child_id, child_left, child_y);
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
        PlaceSubtree(state, root_id, left_column, kMargin);
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