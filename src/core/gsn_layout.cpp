#include "core/gsn_layout.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace core {
namespace {

struct WorkNode {
    const GsnLayoutInputNode* input = nullptr;
    int subtree_width = 1;
    int left_overhang = 0;
    int right_overhang = 0;
    int visit_state = 0;
    int placement_state = 0;
};

struct Placement {
    std::string node_id;
    double column = 0.0;
    int row = 0;
    bool is_group2 = false;
    bool is_left_side = true;
    int stack_index = 0;
};

struct LayoutState {
    std::unordered_map<std::string, WorkNode> nodes;
    std::vector<Placement> placements;
    bool cycle_seen = false;
};

const WorkNode* FindNode(const LayoutState& state, const std::string& id) {
    auto it = state.nodes.find(id);
    if (it == state.nodes.end())
        return nullptr;
    return &it->second;
}

WorkNode* FindNode(LayoutState& state, const std::string& id) {
    auto it = state.nodes.find(id);
    if (it == state.nodes.end())
        return nullptr;
    return &it->second;
}

std::vector<std::string> ExistingChildren(const LayoutState& state, const std::vector<std::string>& ids) {
    std::vector<std::string> children;
    for (const std::string& id : ids) {
        if (FindNode(state, id))
            children.push_back(id);
    }
    return children;
}

int ComputeChildrenSpan(const LayoutState& state,
                        const std::vector<std::string>& children,
                        int start_index,
                        int end_index) {
    int span = 0;
    for (int i = start_index; i < end_index; ++i) {
        const WorkNode* child = FindNode(state, children[i]);
        if (!child)
            continue;
        span += child->subtree_width;
        if (i > start_index) {
            const WorkNode* previous = FindNode(state, children[i - 1]);
            if (previous)
                span += previous->right_overhang + child->left_overhang;
        }
    }
    return span;
}

void ComputeSubtreeInfo(LayoutState& state, const std::string& node_id) {
    WorkNode* node = FindNode(state, node_id);
    if (!node || !node->input)
        return;
    if (node->visit_state == 1) {
        state.cycle_seen = true;
        return;
    }
    if (node->visit_state == 2)
        return;

    node->visit_state = 1;
    const std::vector<std::string> children = ExistingChildren(state, node->input->group1_children);
    for (const std::string& child_id : children)
        ComputeSubtreeInfo(state, child_id);

    if (children.empty()) {
        node->subtree_width = 1;
    } else {
        node->subtree_width = std::max(1, ComputeChildrenSpan(state, children, 0, static_cast<int>(children.size())));
    }

    const int attachment_count = static_cast<int>(ExistingChildren(state, node->input->group2_attachments).size());
    const bool has_left_attachment = attachment_count > 0;
    const bool has_right_attachment = attachment_count >= 2;
    const int own_left = (has_left_attachment && node->subtree_width < 3) ? 1 : 0;
    const int own_right = (has_right_attachment && node->subtree_width < 3) ? 1 : 0;

    int child_left_overhang = 0;
    int child_right_overhang = 0;
    if (!children.empty()) {
        const WorkNode* first = FindNode(state, children.front());
        const WorkNode* last = FindNode(state, children.back());
        child_left_overhang = first ? first->left_overhang : 0;
        child_right_overhang = last ? last->right_overhang : 0;
    }

    node->left_overhang = std::max(own_left, child_left_overhang);
    node->right_overhang = std::max(own_right, child_right_overhang);
    node->visit_state = 2;
}

std::pair<std::vector<int>, std::vector<int>> DistributeAttachmentSides(int count) {
    std::vector<int> left_indices;
    std::vector<int> right_indices;
    const int left_count = (count + 1) / 2;
    for (int i = 0; i < count; ++i) {
        if (i < left_count)
            left_indices.push_back(i);
        else
            right_indices.push_back(i);
    }
    return {left_indices, right_indices};
}

void PlaceGroup2Attachments(LayoutState& state, const WorkNode& node, double column, int row) {
    const std::vector<std::string> attachments = ExistingChildren(state, node.input->group2_attachments);
    if (attachments.empty())
        return;

    auto [left_indices, right_indices] = DistributeAttachmentSides(static_cast<int>(attachments.size()));
    for (int stack_pos = 0; stack_pos < static_cast<int>(left_indices.size()); ++stack_pos) {
        state.placements.push_back({attachments[left_indices[stack_pos]], column - 1.0, row, true, true, stack_pos});
    }
    for (int stack_pos = 0; stack_pos < static_cast<int>(right_indices.size()); ++stack_pos) {
        state.placements.push_back({attachments[right_indices[stack_pos]], column + 1.0, row, true, false, stack_pos});
    }
}

void AssignGridPositions(LayoutState& state, const std::string& node_id, double column, int row) {
    WorkNode* node = FindNode(state, node_id);
    if (!node || !node->input)
        return;
    if (node->placement_state == 1) {
        state.cycle_seen = true;
        return;
    }
    if (node->placement_state == 2)
        return;

    node->placement_state = 1;
    state.placements.push_back({node_id, column, row, false, false, 0});
    PlaceGroup2Attachments(state, *node, column, row);

    const std::vector<std::string> children = ExistingChildren(state, node->input->group1_children);
    if (children.empty()) {
        node->placement_state = 2;
        return;
    }

    const int child_row = row + 1;
    const double total_width =
        static_cast<double>(std::max(1, ComputeChildrenSpan(state, children, 0, static_cast<int>(children.size()))));
    double cursor = column - total_width / 2.0;
    for (int i = 0; i < static_cast<int>(children.size()); ++i) {
        const WorkNode* child = FindNode(state, children[i]);
        if (!child)
            continue;
        if (i > 0) {
            const WorkNode* previous = FindNode(state, children[i - 1]);
            if (previous)
                cursor += static_cast<double>(previous->right_overhang + child->left_overhang);
        }
        const double child_col = cursor + static_cast<double>(child->subtree_width) / 2.0;
        AssignGridPositions(state, children[i], child_col, child_row);
        cursor += static_cast<double>(child->subtree_width);
    }

    node->placement_state = 2;
}

GsnLayoutSize SizeFor(const std::unordered_map<std::string, GsnLayoutSize>& sizes,
                      const GsnLayoutOptions& options,
                      const std::string& id) {
    auto it = sizes.find(id);
    if (it != sizes.end() && it->second.width > 0.0 && it->second.height > 0.0)
        return it->second;
    return {options.base_node_width, options.default_node_height};
}

std::string StackKey(const LayoutState& state, const Placement& placement) {
    const WorkNode* node = FindNode(state, placement.node_id);
    const std::string parent_id = node && node->input ? node->input->parent_id : "";
    return parent_id + "|" + std::to_string(placement.row) + (placement.is_left_side ? "|L" : "|R");
}

void ShiftIntoPositiveCoordinates(std::vector<GsnLayoutNode>& nodes, const GsnLayoutOptions& options) {
    if (nodes.empty())
        return;

    double min_x = nodes.front().x;
    double min_y = nodes.front().y;
    for (const GsnLayoutNode& node : nodes) {
        min_x = std::min(min_x, node.x);
        min_y = std::min(min_y, node.y);
    }

    const double dx = min_x < options.margin_x ? options.margin_x - min_x : 0.0;
    const double dy = min_y < options.margin_y ? options.margin_y - min_y : 0.0;
    if (dx == 0.0 && dy == 0.0)
        return;

    for (GsnLayoutNode& node : nodes) {
        node.x += dx;
        node.y += dy;
    }
}

} // namespace

GsnLayoutGraphResult LayoutGsnGraph(const GsnLayoutInput& input,
                                    const std::unordered_map<std::string, GsnLayoutSize>& node_sizes,
                                    const GsnLayoutOptions& options) {
    GsnLayoutGraphResult result;
    LayoutState state;

    for (const GsnLayoutInputNode& node : input.nodes) {
        if (node.id.empty()) {
            result.warnings.push_back("Layout skipped a node with an empty id.");
            continue;
        }
        auto inserted = state.nodes.emplace(node.id, WorkNode{&node});
        if (!inserted.second)
            result.warnings.push_back("Layout skipped duplicate node id '" + node.id + "'.");
    }

    std::vector<std::string> roots = input.roots;
    if (roots.empty() && !input.nodes.empty())
        roots.push_back(input.nodes.front().id);

    std::unordered_set<std::string> scheduled_roots;
    double next_root_column = 0.0;
    auto schedule_root = [&](const std::string& root_id) {
        WorkNode* root = FindNode(state, root_id);
        if (!root || scheduled_roots.count(root_id) > 0)
            return;
        ComputeSubtreeInfo(state, root_id);
        const double root_column = next_root_column + static_cast<double>(std::max(1, root->subtree_width)) / 2.0;
        AssignGridPositions(state, root_id, root_column, 0);
        next_root_column += static_cast<double>(std::max(1, root->subtree_width)) + 1.0;
        scheduled_roots.insert(root_id);
    };

    for (const std::string& root_id : roots)
        schedule_root(root_id);
    for (const std::string& orphan_id : input.orphans)
        schedule_root(orphan_id);

    if (state.cycle_seen)
        result.warnings.push_back("Layout detected a cycle and used a best-effort traversal.");

    int max_row = 0;
    for (const Placement& placement : state.placements)
        max_row = std::max(max_row, placement.row);

    std::unordered_map<std::string, GsnLayoutSize> resolved_sizes;
    std::unordered_map<std::string, double> group2_stack_offsets;
    std::unordered_map<std::string, double> group2_stack_heights;
    std::unordered_map<int, double> row_group2_stack_height;
    std::vector<double> row_max_height(static_cast<size_t>(max_row + 1), options.default_node_height);
    double max_node_width = options.base_node_width;

    for (const Placement& placement : state.placements) {
        const GsnLayoutSize size = SizeFor(node_sizes, options, placement.node_id);
        resolved_sizes[placement.node_id] = size;
        max_node_width = std::max(max_node_width, size.width);
        if (!placement.is_group2) {
            row_max_height[static_cast<size_t>(placement.row)] =
                std::max(row_max_height[static_cast<size_t>(placement.row)], size.height);
        } else {
            const std::string key = StackKey(state, placement);
            double& stack_height = group2_stack_heights[key];
            if (stack_height > 0.0)
                stack_height += options.side_stack_gap;
            group2_stack_offsets[placement.node_id] = stack_height;
            stack_height += size.height;

            double& row_stack_height = row_group2_stack_height[placement.row];
            row_stack_height = std::max(row_stack_height, stack_height);
        }
    }

    std::vector<double> row_y(static_cast<size_t>(max_row + 1), 0.0);
    std::vector<double> row_heights(static_cast<size_t>(max_row + 1), options.default_node_height);
    double cumulative_y = options.margin_y;
    for (int row = 0; row <= max_row; ++row) {
        row_y[static_cast<size_t>(row)] = cumulative_y;
        const auto stack_it = row_group2_stack_height.find(row);
        const double group2_height = stack_it != row_group2_stack_height.end() ? stack_it->second : 0.0;
        row_heights[static_cast<size_t>(row)] = std::max(row_max_height[static_cast<size_t>(row)], group2_height);
        cumulative_y += row_heights[static_cast<size_t>(row)] + options.vertical_spacing;
    }

    const double column_unit = max_node_width + options.horizontal_spacing;
    for (const Placement& placement : state.placements) {
        const WorkNode* work_node = FindNode(state, placement.node_id);
        if (!work_node || !work_node->input)
            continue;
        const GsnLayoutSize size = SizeFor(resolved_sizes, options, placement.node_id);
        const double row_height = row_heights[static_cast<size_t>(placement.row)];

        GsnLayoutNode node;
        node.id = work_node->input->id;
        node.role = work_node->input->role;
        node.group = work_node->input->group;
        node.label = work_node->input->label;
        node.label_secondary = work_node->input->label_secondary;
        node.undeveloped = work_node->input->undeveloped;
        node.parent_id = work_node->input->parent_id;
        node.width = size.width;
        node.height = size.height;
        node.side_stack_index = placement.stack_index;
        node.is_left_side = placement.is_left_side;
        node.x = options.margin_x + placement.column * column_unit + (max_node_width - node.width) * 0.5;
        node.y = row_y[static_cast<size_t>(placement.row)] + std::max(0.0, (row_height - node.height) * 0.5);

        if (placement.is_group2) {
            const std::string stack_key = StackKey(state, placement);
            const double stack_height = group2_stack_heights[stack_key];
            node.y = row_y[static_cast<size_t>(placement.row)] + std::max(0.0, (row_height - stack_height) * 0.5) +
                     group2_stack_offsets[placement.node_id];
        }

        result.nodes.push_back(std::move(node));
    }

    ShiftIntoPositiveCoordinates(result.nodes, options);
    return result;
}

} // namespace core