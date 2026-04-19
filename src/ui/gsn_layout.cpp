#include "gsn_layout.h"
#include <algorithm>
#include <unordered_map>
#include <cmath>

namespace ui {

// ===== Constants =====
static const float kNodeWidth  = 180.0f;
static const float kNodeHeight =  80.0f;
static const float kHSpacing   =  40.0f;
static const float kVSpacing   =  80.0f;
static const float kLeftMargin =  20.0f;
static const float kTopMargin  =  20.0f;
static const float kSideGap    =  20.0f; // gap between Group2 attachment and parent column

// ===== Helper: map core roles to UI roles =====
static ElementRole to_ui_role(core::NodeRole r) {
    switch (r) {
        case core::NodeRole::Claim:         return ElementRole::Claim;
        case core::NodeRole::Strategy:      return ElementRole::Strategy;
        case core::NodeRole::Solution:      return ElementRole::Solution;
        case core::NodeRole::Context:       return ElementRole::Context;
        case core::NodeRole::Assumption:    return ElementRole::Assumption;
        case core::NodeRole::Justification: return ElementRole::Justification;
        default:                            return ElementRole::Other;
    }
}

// ===== Step 1: Compute subtree widths bottom-up =====
static int compute_subtree_width(core::TreeNode* node) {
    if (node->group1_children.empty()) {
        node->subtree_width = 1;
        return 1;
    }
    int total = 0;
    for (auto* child : node->group1_children) {
        total += compute_subtree_width(child);
    }
    node->subtree_width = total;
    return total;
}

// ===== Group 2 side distribution (spec §10.2.1) =====
// Returns pair: (left_indices, right_indices) into the attachments vector
static std::pair<std::vector<int>, std::vector<int>> distribute_sides(int count) {
    std::vector<int> left, right;
    int n_left = (count + 1) / 2;
    int n_right = count / 2;
    // First n_left go to left, rest to right (in order)
    for (int i = 0; i < count; ++i) {
        if (i < n_left) left.push_back(i);
        else            right.push_back(i);
    }
    return {left, right};
}

// ===== Step 2 & 3: Assign grid positions top-down, then build LayoutNodes =====

struct GridPos {
    float col;   // fractional column position (center of node)
    int row;
};

struct NodePlacement {
    core::TreeNode* node;
    GridPos pos;
    bool is_group2;
    bool is_left_side;
    int stack_index; // for group2 stacking
};

static void layout_recursive(
    core::TreeNode* node,
    float column,
    int row,
    std::vector<NodePlacement>& placements,
    std::unordered_map<int, int>& row_max_stack // row → max group2 stack count on any node in that row
) {
    // Place this node
    placements.push_back({node, {column, row}, false, false, 0});

    // --- Group 2 attachments ---
    int n_att = (int)node->group2_attachments.size();
    if (n_att > 0) {
        auto [left_idx, right_idx] = distribute_sides(n_att);

        // Place left-side attachments
        for (int s = 0; s < (int)left_idx.size(); ++s) {
            core::TreeNode* att = node->group2_attachments[left_idx[s]];
            placements.push_back({att, {column - 1.0f, row}, true, true, s});
        }
        // Place right-side attachments
        for (int s = 0; s < (int)right_idx.size(); ++s) {
            core::TreeNode* att = node->group2_attachments[right_idx[s]];
            placements.push_back({att, {column + 1.0f, row}, true, false, s});
        }

        int max_side = std::max((int)left_idx.size(), (int)right_idx.size());
        if (row_max_stack.find(row) == row_max_stack.end() || row_max_stack[row] < max_side) {
            row_max_stack[row] = max_side;
        }
    }

    // --- Group 1 children ---
    auto& children = node->group1_children;
    if (children.empty()) return;

    if (children.size() == 1) {
        layout_recursive(children[0], column, row + 1, placements, row_max_stack);
    } else {
        // Distribute children using subtree widths
        float total_width = 0.0f;
        for (auto* c : children) {
            total_width += (float)c->subtree_width;
        }

        // Place children so their subtree spans tile next to each other,
        // centered under the parent
        float cursor = column - total_width / 2.0f;
        for (auto* c : children) {
            float child_col = cursor + (float)c->subtree_width / 2.0f;
            layout_recursive(c, child_col, row + 1, placements, row_max_stack);
            cursor += (float)c->subtree_width;
        }
    }
}

// ===== Main tree-based layout =====
std::vector<LayoutNode> LayoutEngine::ComputeLayout(const core::AssuranceTree& tree) {
    std::vector<LayoutNode> result;
    if (!tree.root) return result;

    // Step 1: Compute subtree widths
    compute_subtree_width(tree.root);
    for (auto* orphan : tree.orphans) {
        compute_subtree_width(orphan);
    }

    // Step 2: Layout from root
    std::vector<NodePlacement> placements;
    std::unordered_map<int, int> row_max_stack;

    float root_col = (float)tree.root->subtree_width / 2.0f;
    layout_recursive(tree.root, root_col, 0, placements, row_max_stack);

    // Place orphans at root level (spread to the right of the main tree)
    float orphan_col = (float)tree.root->subtree_width + 1.0f;
    for (auto* orphan : tree.orphans) {
        layout_recursive(orphan, orphan_col, 0, placements, row_max_stack);
        orphan_col += (float)orphan->subtree_width + 1.0f;
    }

    // Step 3: Compute row Y positions (variable height due to Group2 stacking)
    int max_row = 0;
    for (const auto& p : placements) {
        if (p.pos.row > max_row) max_row = p.pos.row;
    }

    std::vector<float> row_y(max_row + 1, 0.0f);
    float y = kTopMargin;
    for (int r = 0; r <= max_row; ++r) {
        row_y[r] = y;
        int stack_count = 1;
        if (row_max_stack.find(r) != row_max_stack.end()) {
            stack_count = std::max(stack_count, row_max_stack[r]);
        }
        y += (float)stack_count * (kNodeHeight + kVSpacing);
    }

    // Step 4: Convert grid positions to pixel positions
    const ImVec2 node_size(kNodeWidth, kNodeHeight);
    float col_unit = kNodeWidth + kHSpacing;

    for (const auto& p : placements) {
        LayoutNode ln;
        ln.id = p.node->id;
        ln.role = to_ui_role(p.node->role);
        ln.group = (p.node->group == core::ElementGroup::Group1) ? ElementGroup::Group1 : ElementGroup::Group2;
        ln.label = p.node->label;
        ln.size = node_size;
        ln.parent_id = p.node->parent ? p.node->parent->id : "";
        ln.is_left_side = p.is_left_side;
        ln.side_stack_index = p.stack_index;

        float x = kLeftMargin + p.pos.col * col_unit;
        float base_y = row_y[p.pos.row];

        if (p.is_group2) {
            // Stack vertically from the base row position
            base_y += (float)p.stack_index * (kNodeHeight + kSideGap);
        }

        ln.position = ImVec2(x, base_y);
        result.push_back(ln);
    }

    return result;
}

// ===== Legacy flat layout (unchanged placeholder) =====
std::vector<LayoutNode> LayoutEngine::ComputeLayout(const std::vector<CanvasElement>& elements) {
    std::vector<LayoutNode> nodes;

    const ImVec2 node_size = ImVec2(kNodeWidth, kNodeHeight);

    std::vector<CanvasElement> claims;
    std::vector<CanvasElement> others;
    for (const auto& e : elements) {
        if (e.role == ElementRole::Claim) claims.push_back(e);
        else others.push_back(e);
    }

    for (size_t i = 0; i < claims.size(); ++i) {
        LayoutNode n;
        n.id = claims[i].id;
        n.role = claims[i].role;
        n.label = claims[i].label;
        n.size = node_size;
        n.position = ImVec2(kLeftMargin + (float)i * (kNodeWidth + kHSpacing), kTopMargin);
        n.parent_id = claims[i].parent_id;
        nodes.push_back(n);
    }

    auto place_row = [&](ElementRole role, size_t row_index, const std::vector<CanvasElement>& pool) {
        std::vector<CanvasElement> row_elems;
        for (const auto& e : pool) if (e.role == role) row_elems.push_back(e);
        for (size_t i = 0; i < row_elems.size(); ++i) {
            LayoutNode n;
            n.id = row_elems[i].id;
            n.role = row_elems[i].role;
            n.label = row_elems[i].label;
            n.size = node_size;
            n.position = ImVec2(kLeftMargin + (float)i * (kNodeWidth + kHSpacing), kTopMargin + (float)(row_index) * (kNodeHeight + kVSpacing));
            n.parent_id = row_elems[i].parent_id;
            nodes.push_back(n);
        }
    };

    std::vector<ElementRole> below_order = { ElementRole::Strategy, ElementRole::SubClaim, ElementRole::Solution, ElementRole::Evidence, ElementRole::Context, ElementRole::Justification, ElementRole::Assumption, ElementRole::Other };
    for (size_t r = 0; r < below_order.size(); ++r) {
        place_row(below_order[r], 1 + r, others);
    }

    return nodes;
}

} // namespace ui
