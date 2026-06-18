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

// ChallengeSide (Left/Right/Below) is defined in core/assurance_tree.h.

// A challenge cluster hosted by a node: `root_id` is the counter element (the
// root of its own sub-argument, which lives in the input as ordinary nodes).
struct ChallengeChild {
    std::string root_id;
    ChallengeSide side = ChallengeSide::Side;
};

struct GsnLayoutInputNode {
    std::string id;
    NodeRole role = NodeRole::Other;
    ElementGroup group = ElementGroup::Group1;
    std::string label;
    std::string label_secondary;
    bool undeveloped = false;
    bool uninstantiated = false;
    std::string parent_id;
    std::vector<std::string> group1_children;
    std::vector<std::string> group2_attachments;
    // Dialectic challenge clusters hosted by this node. Their roots and
    // descendants appear in GsnLayoutInput::nodes but are reachable only here.
    std::vector<ChallengeChild> challenge_children;
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
    bool uninstantiated = false;
    std::string parent_id;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    int side_stack_index = 0;
    bool is_left_side = true;
    // Distance (content px) the edges from this node to its structural children
    // should run straight down before curving — large when children are pushed
    // below a tall side challenge tree, so the lines clear the side margin.
    double child_edge_drop = 0.0;
};

struct GsnLayoutGraphResult {
    std::vector<GsnLayoutNode> nodes;
    std::vector<std::string> warnings;
};

GsnLayoutGraphResult LayoutGsnGraph(const GsnLayoutInput& input,
                                    const std::unordered_map<std::string, GsnLayoutSize>& node_sizes,
                                    const GsnLayoutOptions& options = {});

} // namespace core