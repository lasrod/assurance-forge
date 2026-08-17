#include "export/gsn_svg_layout.h"

#include "core/gsn_layout.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace export_gsn {
namespace {

constexpr double kMargin = 48.0;
constexpr double kSideStackGap = 20.0;
constexpr double kSupportGap = 88.0;
constexpr double kTextLineHeight = 18.0;
constexpr double kTextCharWidth = 7.0;
constexpr double kTextVerticalPadding = 38.0;
constexpr double kSolutionTextRatio = 0.72;        // usable text height as a fraction of the circle diameter
constexpr double kSolutionDiameterTolerance = 1.0; // bisection precision for the circle diameter
constexpr int kSolutionGrowthDoublings = 8;        // bound on the search for a diameter that fits

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

std::string DisplayTextForLayout(const GsnNode& node) {
    std::string label = node.display_id.empty() ? node.id : node.display_id;
    if (!node.title.empty())
        label += ": " + node.title;
    if (!node.text.empty()) {
        if (!label.empty())
            label += "\n";
        label += node.text;
    }
    return label;
}

size_t WrappedLineCount(const GsnNode& node, double width) {
    const double available_width = node.kind == GsnNodeKind::Solution ? width * 0.62 : width - 36.0;
    const size_t max_chars = std::max<size_t>(8, static_cast<size_t>(available_width / kTextCharWidth));
    size_t lines = 0;

    std::istringstream paragraphs(DisplayTextForLayout(node));
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        std::istringstream words(paragraph);
        std::string word;
        size_t line_chars = 0;
        while (words >> word) {
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
        ++lines;
    }

    return lines;
}

bool SolutionTextFits(const GsnNode& node, double diameter) {
    const double required_height =
        static_cast<double>(WrappedLineCount(node, diameter)) * kTextLineHeight + kTextVerticalPadding;
    return required_height <= diameter * kSolutionTextRatio;
}

// Smallest diameter at or above `base_diameter` that holds the wrapped text.
//
// The trap the canvas renderer also fell into: a circle's text box widens with
// the circle, so line counts taken at the base width size a disc far larger
// than the text needs. Re-count at each candidate diameter instead. The line
// count never grows with the diameter, so bisection converges.
double SolutionDiameter(const GsnNode& node, double base_diameter) {
    if (SolutionTextFits(node, base_diameter))
        return base_diameter;

    double low = base_diameter;
    double high = base_diameter * 2.0;
    for (int i = 0; i < kSolutionGrowthDoublings; ++i) {
        if (SolutionTextFits(node, high))
            break;
        low = high;
        high *= 2.0;
    }
    while (high - low > kSolutionDiameterTolerance) {
        const double mid = (low + high) * 0.5;
        if (SolutionTextFits(node, mid))
            high = mid;
        else
            low = mid;
    }
    return high;
}

void ApplyNodeSize(GsnNode& node) {
    const NodeSizeLimits limits = SizeLimitsFor(node.kind);
    if (node.kind == GsnNodeKind::Solution) {
        const double diameter = SolutionDiameter(node, std::max(limits.base_width, limits.base_height));
        node.width = diameter;
        node.height = diameter;
        return;
    }

    double width = limits.base_width;
    size_t line_count = WrappedLineCount(node, width);
    while (width < limits.max_width && line_count > 5) {
        width = std::min(limits.max_width, width + 30.0);
        line_count = WrappedLineCount(node, width);
    }

    const double required_height = static_cast<double>(line_count) * kTextLineHeight + kTextVerticalPadding;
    node.width = width;
    node.height = std::max(limits.base_height, required_height);
}

core::NodeRole ToCoreRole(GsnNodeKind kind) {
    switch (kind) {
    case GsnNodeKind::Goal:
        return core::NodeRole::Claim;
    case GsnNodeKind::Strategy:
        return core::NodeRole::Strategy;
    case GsnNodeKind::Solution:
        return core::NodeRole::Solution;
    case GsnNodeKind::Context:
        return core::NodeRole::Context;
    case GsnNodeKind::Assumption:
        return core::NodeRole::Assumption;
    case GsnNodeKind::Justification:
        return core::NodeRole::Justification;
    }
    return core::NodeRole::Other;
}

core::ElementGroup ToCoreGroup(GsnNodeKind kind) {
    return IsSideInformation(kind) ? core::ElementGroup::Group2 : core::ElementGroup::Group1;
}

// The node a layout edge attaches to. Normally the edge's own target, but a
// challenge aimed at a relationship has no node target -- it lands on another
// edge. That edge's `to_id` is the SACM relationship's *source* (endpoints are
// swapped for SupportedBy/InContextOf), which is the element the canvas hosts
// such a challenge on: challenging "InContextOf C1 -> G1" sits the counter with
// C1, not with G1.
std::string ResolveLayoutAnchorId(const GsnEdge& edge,
                                  const std::unordered_map<std::string, const GsnEdge*>& edge_by_id) {
    if (edge.to_edge_id.empty())
        return edge.to_id;
    auto target = edge_by_id.find(edge.to_edge_id);
    return target != edge_by_id.end() ? target->second->to_id : std::string{};
}

} // namespace

GsnSvgLayoutResult LayoutGsnSvgDiagram(GsnDiagram& diagram) {
    GsnSvgLayoutResult result;
    core::GsnLayoutInput input;
    std::unordered_map<std::string, size_t> node_by_id;
    std::unordered_map<std::string, core::GsnLayoutSize> node_sizes;
    std::unordered_map<std::string, core::GsnLayoutInputNode> input_nodes;
    std::unordered_set<std::string> support_child_ids;
    std::unordered_set<std::string> side_child_ids;

    for (size_t i = 0; i < diagram.nodes.size(); ++i) {
        ApplyNodeSize(diagram.nodes[i]);
        node_by_id[diagram.nodes[i].id] = i;
        node_sizes[diagram.nodes[i].id] = {diagram.nodes[i].width, diagram.nodes[i].height};

        core::GsnLayoutInputNode node;
        node.id = diagram.nodes[i].id;
        node.role = ToCoreRole(diagram.nodes[i].kind);
        node.group = ToCoreGroup(diagram.nodes[i].kind);
        node.label = DisplayTextForLayout(diagram.nodes[i]);
        input_nodes[node.id] = std::move(node);
    }

    std::unordered_map<std::string, const GsnEdge*> edge_by_id;
    for (const GsnEdge& edge : diagram.edges)
        edge_by_id[edge.id] = &edge;

    for (const GsnEdge& edge : diagram.edges) {
        const std::string anchor_id = ResolveLayoutAnchorId(edge, edge_by_id);
        auto from_node = input_nodes.find(edge.from_id);
        auto to_node = input_nodes.find(anchor_id);
        if (from_node == input_nodes.end() || to_node == input_nodes.end()) {
            result.warnings.push_back("Layout skipped edge '" + edge.id + "' because an endpoint was missing.");
            continue;
        }
        if (edge.kind == GsnEdgeKind::Challenges) {
            // A challenge is not support. Wiring it as a parent/child edge would
            // place counter-evidence inside the support tree of the very claim it
            // attacks. Side-attaching the counter to its anchor puts it beside
            // what it challenges, which is how the canvas lays it out.
            if (side_child_ids.count(edge.from_id) > 0 || support_child_ids.count(edge.from_id) > 0)
                continue; // already placed; a node has one position
            to_node->second.group2_attachments.push_back(edge.from_id);
            from_node->second.parent_id = anchor_id;
            from_node->second.group = core::ElementGroup::Group2;
            side_child_ids.insert(edge.from_id);
        } else if (edge.kind == GsnEdgeKind::InContextOf) {
            from_node->second.group2_attachments.push_back(edge.to_id);
            to_node->second.parent_id = edge.from_id;
            to_node->second.group = core::ElementGroup::Group2;
            side_child_ids.insert(edge.to_id);
        } else {
            from_node->second.group1_children.push_back(edge.to_id);
            to_node->second.parent_id = edge.from_id;
            to_node->second.group = core::ElementGroup::Group1;
            support_child_ids.insert(edge.to_id);
        }
    }

    for (const GsnNode& node : diagram.nodes) {
        if (support_child_ids.find(node.id) == support_child_ids.end() &&
            side_child_ids.find(node.id) == side_child_ids.end()) {
            input.roots.push_back(node.id);
        }
    }
    if (input.roots.empty() && !diagram.nodes.empty()) {
        input.roots.push_back(diagram.nodes.front().id);
        result.warnings.push_back("Layout fallback was used because no root goal could be determined.");
    }

    for (const GsnNode& node : diagram.nodes) {
        if (input_nodes.find(node.id) != input_nodes.end())
            input.nodes.push_back(std::move(input_nodes[node.id]));
    }

    core::GsnLayoutOptions options;
    options.margin_x = kMargin;
    options.margin_y = kMargin;
    options.base_node_width = 260.0;
    options.default_node_height = 100.0;
    options.horizontal_spacing = 88.0;
    options.vertical_spacing = kSupportGap;
    options.side_stack_gap = kSideStackGap;

    core::GsnLayoutGraphResult layout = core::LayoutGsnGraph(input, node_sizes, options);
    result.warnings.insert(result.warnings.end(), layout.warnings.begin(), layout.warnings.end());

    for (const core::GsnLayoutNode& layout_node : layout.nodes) {
        auto node_it = node_by_id.find(layout_node.id);
        if (node_it == node_by_id.end())
            continue;
        GsnNode& node = diagram.nodes[node_it->second];
        node.x = layout_node.x;
        node.y = layout_node.y;
        node.width = layout_node.width;
        node.height = layout_node.height;
    }
    return result;
}

} // namespace export_gsn
