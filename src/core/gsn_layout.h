#pragma once

#include "core/assurance_tree.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace core {

struct GsnLayoutSize {
    double width = 0.0;
    double height = 0.0;
};

struct GsnLayoutInputNode {
    std::string id;
    NodeRole role = NodeRole::Other;
    ElementGroup group = ElementGroup::Group1;
    std::string label;
    std::string label_secondary;
    bool undeveloped = false;
    std::string parent_id;
    std::vector<std::string> group1_children;
    std::vector<std::string> group2_attachments;
};

struct GsnLayoutInput {
    std::vector<GsnLayoutInputNode> nodes;
    std::vector<std::string> roots;
    std::vector<std::string> orphans;
};

struct GsnLayoutOptions {
    double margin_x = 20.0;
    double margin_y = 20.0;
    double base_node_width = 260.0;
    double default_node_height = 100.0;
    double horizontal_spacing = 40.0;
    double vertical_spacing = 80.0;
    double side_stack_gap = 20.0;
};

struct GsnLayoutNode {
    std::string id;
    NodeRole role = NodeRole::Other;
    ElementGroup group = ElementGroup::Group1;
    std::string label;
    std::string label_secondary;
    bool undeveloped = false;
    std::string parent_id;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    int side_stack_index = 0;
    bool is_left_side = true;
};

struct GsnLayoutGraphResult {
    std::vector<GsnLayoutNode> nodes;
    std::vector<std::string> warnings;
};

GsnLayoutGraphResult LayoutGsnGraph(const GsnLayoutInput& input,
                                    const std::unordered_map<std::string, GsnLayoutSize>& node_sizes,
                                    const GsnLayoutOptions& options = {});

} // namespace core