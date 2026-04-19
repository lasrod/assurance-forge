#include "gsn_layout.h"
#include "gsn_canvas.h" // for g_BoldFont
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cstring>

namespace ui {

// Bold font pointer shared between layout engine and node drawing code.
// Set by main.cpp at startup.
ImFont* g_BoldFont = nullptr;

// ===== Constants =====
static const float kNodeWidth      = 220.0f;
static const float kNodeHeight     = 100.0f;
static const float kSolutionWidth  = 160.0f;  // circle diameter for Solution/Evidence
static const float kSolutionHeight = 160.0f;
static const float kHSpacing   =  40.0f;
static const float kVSpacing   =  80.0f;
static const float kLeftMargin =  20.0f;
static const float kTopMargin  =  20.0f;
static const float kSideGap    =  20.0f; // gap between Group2 attachment and parent column

// ===== Compute node size based on text content =====
static ImVec2 ComputeNodeSize(const std::string& label, core::NodeRole role) {
    bool is_solution = (role == core::NodeRole::Solution);
    float base_w = is_solution ? kSolutionWidth : kNodeWidth;
    float base_h = is_solution ? kSolutionHeight : kNodeHeight;

    // If no ImGui context (e.g. in tests), use base size
    if (!ImGui::GetCurrentContext() || !ImGui::GetFont()) {
        return ImVec2(base_w, base_h);
    }

    float pad = 6.0f;
    float font_size = ImGui::GetFontSize();
    ImFont* bold = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal = ImGui::GetFont();

    // Compute effective text wrap width per shape type (matches gsn_canvas.cpp)
    float text_wrap = base_w - pad * 2.0f;
    if (role == core::NodeRole::Strategy) {
        float skew = base_w * 0.15f;
        text_wrap = base_w - skew * 2.0f - pad * 2.0f;
    } else if (is_solution) {
        float radius = base_w * 0.5f;
        float inset = radius * 0.29f;
        text_wrap = (radius - inset) * 2.0f - pad * 2.0f;
    } else if (role == core::NodeRole::Context || role == core::NodeRole::Assumption || role == core::NodeRole::Justification) {
        float inset = base_h * 0.15f;
        text_wrap = base_w - inset * 2.0f - pad * 2.0f;
    }
    if (text_wrap < 40.0f) text_wrap = 40.0f;

    // Measure text height
    const char* cstr = label.c_str();
    const char* nl = strchr(cstr, '\n');
    ImVec2 bold_size(0, 0);
    ImVec2 rest_size(0, 0);
    if (nl) {
        bold_size = bold->CalcTextSizeA(font_size, FLT_MAX, text_wrap, cstr, nl);
        rest_size = normal->CalcTextSizeA(font_size, FLT_MAX, text_wrap, nl + 1, nullptr);
    } else {
        bold_size = bold->CalcTextSizeA(font_size, FLT_MAX, text_wrap, cstr, nullptr);
    }
    float text_h = bold_size.y + rest_size.y + pad * 2.0f;

    // Grow height if text overflows, keeping width fixed (snapped to row grid)
    float min_h = base_h;
    if (is_solution) {
        // For circles, grow diameter to fit text (text area ~ 0.7 * diameter)
        float needed_diam = text_h / 0.7f;
        if (needed_diam > min_h) min_h = needed_diam;
        // Keep square
        return ImVec2(std::max(base_w, min_h), min_h);
    }
    if (text_h > min_h) min_h = text_h;
    return ImVec2(base_w, min_h);
}

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

// ===== Step 1: Compute subtree widths and Group2 overhang bottom-up =====
static void compute_subtree_info(core::TreeNode* node) {
    // Recurse into Group1 children first
    for (auto* child : node->group1_children) {
        compute_subtree_info(child);
    }

    // Compute base width from Group1 children, adding gaps between adjacent
    // siblings to prevent their Group2 attachments from overlapping.
    auto& children = node->group1_children;
    if (children.empty()) {
        node->subtree_width = 1;
    } else if (children.size() == 1) {
        node->subtree_width = children[0]->subtree_width;
    } else if (children.size() % 2 == 1) {
        // Odd children: the middle child will be centered under the parent.
        // Compute left_arm and right_arm (space needed on each side of mid).
        int mid = (int)children.size() / 2;
        int left_arm = 0;
        for (int i = 0; i < mid; i++) {
            left_arm += children[i]->subtree_width;
            if (i > 0) {
                left_arm += children[i - 1]->right_overhang
                          + children[i]->left_overhang;
            }
        }
        // gap between leftmost-of-left and middle
        left_arm += children[mid - 1]->right_overhang + children[mid]->left_overhang;

        int right_arm = 0;
        for (int i = mid + 1; i < (int)children.size(); i++) {
            right_arm += children[i]->subtree_width;
            if (i > mid + 1) {
                right_arm += children[i - 1]->right_overhang
                           + children[i]->left_overhang;
            }
        }
        // gap between middle and leftmost-of-right
        right_arm += children[mid]->right_overhang + children[mid + 1]->left_overhang;

        // Width must accommodate both arms symmetrically from center
        int max_arm = std::max(left_arm, right_arm);
        node->subtree_width = children[mid]->subtree_width + 2 * max_arm;
    } else {
        // Even children: sequential layout
        int total = 0;
        for (int i = 0; i < (int)children.size(); i++) {
            total += children[i]->subtree_width;
            if (i > 0) {
                total += children[i - 1]->right_overhang
                       + children[i]->left_overhang;
            }
        }
        node->subtree_width = total;
    }

    // Determine whether this node has Group2 attachments on each side
    int n_att = (int)node->group2_attachments.size();
    bool has_left_g2  = (n_att > 0);                // first attachment goes left
    bool has_right_g2 = (n_att >= 2);               // second+ goes right

    // Own overhang: if the subtree is too narrow (< 2 columns) the Group2
    // node at column ± 1 extends beyond the subtree boundary.
    int own_left  = (has_left_g2  && node->subtree_width < 2) ? 1 : 0;
    int own_right = (has_right_g2 && node->subtree_width < 2) ? 1 : 0;

    // Propagate overhang from the leftmost / rightmost child.
    // For odd-centered layouts, the shorter arm has padding that absorbs
    // some child overhang, so we subtract it.
    int child_left  = 0;
    int child_right = 0;
    if (!children.empty()) {
        child_left  = children.front()->left_overhang;
        child_right = children.back()->right_overhang;

        if (children.size() > 1 && children.size() % 2 == 1) {
            int mid = (int)children.size() / 2;
            int left_arm = 0;
            for (int i = 0; i < mid; i++) {
                left_arm += children[i]->subtree_width;
                if (i > 0)
                    left_arm += children[i - 1]->right_overhang + children[i]->left_overhang;
            }
            left_arm += children[mid - 1]->right_overhang + children[mid]->left_overhang;

            int right_arm = 0;
            for (int i = mid + 1; i < (int)children.size(); i++) {
                right_arm += children[i]->subtree_width;
                if (i > mid + 1)
                    right_arm += children[i - 1]->right_overhang + children[i]->left_overhang;
            }
            right_arm += children[mid]->right_overhang + children[mid + 1]->left_overhang;

            int max_arm = std::max(left_arm, right_arm);
            int left_padding  = max_arm - left_arm;
            int right_padding = max_arm - right_arm;
            child_left  = std::max(0, child_left  - left_padding);
            child_right = std::max(0, child_right - right_padding);
        }
    }

    node->left_overhang  = std::max(own_left,  child_left);
    node->right_overhang = std::max(own_right, child_right);
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
    } else if (children.size() % 2 == 1) {
        // Odd number of children: anchor the middle child directly under
        // the parent, then lay out left and right halves outward.
        int mid = (int)children.size() / 2;

        // Place middle child centered on parent column
        layout_recursive(children[mid], column, row + 1, placements, row_max_stack);

        // Left half: place rightmost-to-leftmost, growing leftward from middle
        float cursor = column - (float)children[mid]->subtree_width / 2.0f;
        for (int i = mid - 1; i >= 0; --i) {
            cursor -= (float)(children[i]->right_overhang + children[i + 1]->left_overhang);
            float child_col = cursor - (float)children[i]->subtree_width / 2.0f;
            layout_recursive(children[i], child_col, row + 1, placements, row_max_stack);
            cursor -= (float)children[i]->subtree_width;
        }

        // Right half: place leftmost-to-rightmost, growing rightward from middle
        cursor = column + (float)children[mid]->subtree_width / 2.0f;
        for (int i = mid + 1; i < (int)children.size(); ++i) {
            cursor += (float)(children[i - 1]->right_overhang + children[i]->left_overhang);
            float child_col = cursor + (float)children[i]->subtree_width / 2.0f;
            layout_recursive(children[i], child_col, row + 1, placements, row_max_stack);
            cursor += (float)children[i]->subtree_width;
        }
    } else {
        // Even number of children: distribute symmetrically using total width.
        float total_width = 0.0f;
        for (int i = 0; i < (int)children.size(); i++) {
            total_width += (float)children[i]->subtree_width;
            if (i > 0) {
                total_width += (float)(children[i - 1]->right_overhang
                                     + children[i]->left_overhang);
            }
        }

        float cursor = column - total_width / 2.0f;
        for (int i = 0; i < (int)children.size(); i++) {
            if (i > 0) {
                cursor += (float)(children[i - 1]->right_overhang
                                + children[i]->left_overhang);
            }
            float child_col = cursor + (float)children[i]->subtree_width / 2.0f;
            layout_recursive(children[i], child_col, row + 1, placements, row_max_stack);
            cursor += (float)children[i]->subtree_width;
        }
    }
}

// ===== Main tree-based layout =====
std::vector<LayoutNode> LayoutEngine::ComputeLayout(const core::AssuranceTree& tree) {
    std::vector<LayoutNode> result;
    if (!tree.root) return result;

    // Step 1: Compute subtree widths and Group2 overhang
    compute_subtree_info(tree.root);
    for (auto* orphan : tree.orphans) {
        compute_subtree_info(orphan);
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

    // Step 3: Compute per-node sizes and track max height per row
    int max_row = 0;
    for (const auto& p : placements) {
        if (p.pos.row > max_row) max_row = p.pos.row;
    }

    std::unordered_map<std::string, ImVec2> node_sizes;
    std::vector<float> row_max_height(max_row + 1, kNodeHeight);

    for (const auto& p : placements) {
        ImVec2 sz = ComputeNodeSize(p.node->label, p.node->role);
        node_sizes[p.node->id] = sz;
        if (!p.is_group2 && sz.y > row_max_height[p.pos.row]) {
            row_max_height[p.pos.row] = sz.y;
        }
    }

    // Step 4: Compute row Y positions (variable height due to Group2 stacking and tall nodes)
    std::vector<float> row_y(max_row + 1, 0.0f);
    float y = kTopMargin;
    for (int r = 0; r <= max_row; ++r) {
        row_y[r] = y;
        int stack_count = 1;
        if (row_max_stack.find(r) != row_max_stack.end()) {
            stack_count = std::max(stack_count, row_max_stack[r]);
        }
        float row_h = std::max(row_max_height[r], (float)stack_count * (kNodeHeight + kSideGap) - kSideGap);
        y += row_h + kVSpacing;
    }

    // Step 5: Convert grid positions to pixel positions
    float col_unit = kNodeWidth + kHSpacing;

    for (const auto& p : placements) {
        LayoutNode ln;
        ln.id = p.node->id;
        ln.role = to_ui_role(p.node->role);
        ln.group = (p.node->group == core::ElementGroup::Group1) ? ElementGroup::Group1 : ElementGroup::Group2;
        ln.label = p.node->label;
        ln.size = node_sizes[p.node->id];
        ln.parent_id = p.node->parent ? p.node->parent->id : "";
        ln.is_left_side = p.is_left_side;
        ln.side_stack_index = p.stack_index;

        float x = kLeftMargin + p.pos.col * col_unit;
        // Center solution circle within the column
        bool is_solution = (p.node->role == core::NodeRole::Solution);
        if (is_solution) {
            x += (kNodeWidth - ln.size.x) * 0.5f;
        }
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
