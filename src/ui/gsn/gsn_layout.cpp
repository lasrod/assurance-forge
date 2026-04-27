#include "ui/gsn/gsn_layout.h"
#include "ui/gsn/gsn_canvas.h" // for g_BoldFont
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cstring>

namespace ui::gsn {

// Bold font pointer shared between layout engine and node drawing code.
// Set by main.cpp at startup.
ImFont* g_BoldFont = nullptr;

// ===== Layout constants =====
static constexpr float kNodeWidth      = 220.0f;  // default node width (px)
static constexpr float kNodeHeight     = 100.0f;  // default node height (px)
static constexpr float kSolutionWidth  = 160.0f;  // circle diameter for Solution/Evidence nodes
static constexpr float kSolutionHeight = 160.0f;
static constexpr float kHSpacing       =  40.0f;  // horizontal gap between adjacent columns
static constexpr float kVSpacing       =  80.0f;  // vertical gap between adjacent rows
static constexpr float kLeftMargin     =  20.0f;  // canvas left margin
static constexpr float kTopMargin      =  20.0f;  // canvas top margin
static constexpr float kSideGap        =  20.0f;  // vertical gap between stacked Group2 nodes

// Text measurement constants (must match gsn_canvas.cpp drawing insets)
static constexpr float kTextPadding        = 6.0f;   // inner padding between shape edge and text
static constexpr float kMinTextWrap        = 40.0f;  // minimum text wrap width
static constexpr float kParallelogramSkew  = 0.15f;  // fraction of width for Strategy shape inset
static constexpr float kCircleTextInset    = 0.29f;  // fraction of radius for circle text area
static constexpr float kStadiumTextInset   = 0.15f;  // fraction of height for stadium text inset
static constexpr float kCircleTextRatio    = 0.7f;   // effective text height as fraction of circle diameter

// ===== Compute node size based on text content =====
// Measures text using ImGui font metrics and returns the required node dimensions.
// Width stays fixed at the base size; height grows if text overflows.
static ImVec2 ComputeNodeSize(const std::string& label, core::NodeRole role) {
    bool is_solution = (role == core::NodeRole::Solution);
    float base_width  = is_solution ? kSolutionWidth : kNodeWidth;
    float base_height = is_solution ? kSolutionHeight : kNodeHeight;

    // If no ImGui context (e.g. in unit tests), use base size
    if (!ImGui::GetCurrentContext() || !ImGui::GetFont()) {
        return ImVec2(base_width, base_height);
    }

    float font_size = ImGui::GetFontSize();
    ImFont* bold_font   = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal_font = ImGui::GetFont();

    // Compute text wrap width matching the drawing insets in gsn_canvas.cpp
    float text_wrap = base_width - kTextPadding * 2.0f;
    if (role == core::NodeRole::Strategy) {
        float skew = base_width * kParallelogramSkew;
        text_wrap = base_width - skew * 2.0f - kTextPadding * 2.0f;
    } else if (is_solution) {
        float radius = base_width * 0.5f;
        float inset = radius * kCircleTextInset;
        text_wrap = (radius - inset) * 2.0f - kTextPadding * 2.0f;
    } else if (role == core::NodeRole::Context || role == core::NodeRole::Assumption || role == core::NodeRole::Justification) {
        float inset = base_height * kStadiumTextInset;
        text_wrap = base_width - inset * 2.0f - kTextPadding * 2.0f;
    }
    if (text_wrap < kMinTextWrap) text_wrap = kMinTextWrap;

    // Measure text height (bold first line + normal rest)
    const char* label_start = label.c_str();
    const char* first_newline = strchr(label_start, '\n');
    ImVec2 bold_text_size(0, 0);
    ImVec2 rest_text_size(0, 0);
    if (first_newline) {
        bold_text_size = bold_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_start, first_newline);
        rest_text_size = normal_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, first_newline + 1, nullptr);
    } else {
        bold_text_size = bold_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_start, nullptr);
    }
    float required_text_height = bold_text_size.y + rest_text_size.y + kTextPadding * 2.0f;

    // Grow height if text overflows, keeping width fixed
    float final_height = base_height;
    if (is_solution) {
        // For circles, grow diameter so text area (kCircleTextRatio * diameter) fits
        float needed_diameter = required_text_height / kCircleTextRatio;
        if (needed_diameter > final_height) final_height = needed_diameter;
        // Keep square (width = height for circles)
        return ImVec2(std::max(base_width, final_height), final_height);
    }
    if (required_text_height > final_height) final_height = required_text_height;
    return ImVec2(base_width, final_height);
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

// Compute the total column span needed for a contiguous range of children,
// accounting for Group2 overhangs between adjacent siblings.
// Range is [start_index, end_index) within the children vector.
static int ComputeChildrenSpan(const std::vector<core::TreeNode*>& children,
                               int start_index, int end_index) {
    int span = 0;
    for (int i = start_index; i < end_index; ++i) {
        span += children[i]->subtree_width;
        if (i > start_index) {
            span += children[i - 1]->right_overhang + children[i]->left_overhang;
        }
    }
    return span;
}

// Compute the gap between the middle child and an adjacent half.
// For left half: gap = children[mid-1]->right_overhang + children[mid]->left_overhang
// For right half: gap = children[mid]->right_overhang + children[mid+1]->left_overhang
static int ComputeArmWidth(const std::vector<core::TreeNode*>& children,
                           int mid, bool is_left_arm) {
    if (is_left_arm) {
        int arm = ComputeChildrenSpan(children, 0, mid);
        arm += children[mid - 1]->right_overhang + children[mid]->left_overhang;
        return arm;
    } else {
        int arm = ComputeChildrenSpan(children, mid + 1, (int)children.size());
        arm += children[mid]->right_overhang + children[mid + 1]->left_overhang;
        return arm;
    }
}

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
        // Odd children: the middle child is centered under the parent.
        // Each arm extends from center to the outermost child on that side.
        int mid = (int)children.size() / 2;
        int left_arm  = ComputeArmWidth(children, mid, /*is_left_arm=*/true);
        int right_arm = ComputeArmWidth(children, mid, /*is_left_arm=*/false);
        // Width must accommodate both arms symmetrically from center
        int max_arm = std::max(left_arm, right_arm);
        node->subtree_width = children[mid]->subtree_width + 2 * max_arm;
    } else {
        // Even children: sequential layout
        node->subtree_width = ComputeChildrenSpan(children, 0, (int)children.size());
    }

    // Determine whether this node has Group2 attachments on each side
    int attachment_count = (int)node->group2_attachments.size();
    bool has_left_attachment  = (attachment_count > 0);   // first attachment goes left
    bool has_right_attachment = (attachment_count >= 2);   // second+ goes right

    // Own overhang: if the subtree is too narrow (< 2 columns) the Group2
    // node at column Â± 1 extends beyond the subtree boundary.
    int own_left  = (has_left_attachment  && node->subtree_width < 2) ? 1 : 0;
    int own_right = (has_right_attachment && node->subtree_width < 2) ? 1 : 0;

    // Propagate overhang from the leftmost / rightmost child.
    // For odd-centered layouts, the shorter arm has padding that absorbs
    // some child overhang, so we subtract it.
    int child_left_overhang  = 0;
    int child_right_overhang = 0;
    if (!children.empty()) {
        child_left_overhang  = children.front()->left_overhang;
        child_right_overhang = children.back()->right_overhang;

        if (children.size() > 1 && children.size() % 2 == 1) {
            int mid = (int)children.size() / 2;
            int left_arm  = ComputeArmWidth(children, mid, /*is_left_arm=*/true);
            int right_arm = ComputeArmWidth(children, mid, /*is_left_arm=*/false);
            int max_arm = std::max(left_arm, right_arm);
            int left_padding  = max_arm - left_arm;
            int right_padding = max_arm - right_arm;
            child_left_overhang  = std::max(0, child_left_overhang  - left_padding);
            child_right_overhang = std::max(0, child_right_overhang - right_padding);
        }
    }

    node->left_overhang  = std::max(own_left,  child_left_overhang);
    node->right_overhang = std::max(own_right, child_right_overhang);
}

// ===== Group 2 side distribution (GSN spec Â§10.2.1) =====
// Distributes N attachments between left and right sides: ceil(N/2) left, floor(N/2) right.
// Returns pair: (left_indices, right_indices) into the attachments vector.
static std::pair<std::vector<int>, std::vector<int>> DistributeAttachmentSides(int count) {
    std::vector<int> left_indices, right_indices;
    int left_count = (count + 1) / 2;
    for (int i = 0; i < count; ++i) {
        if (i < left_count) left_indices.push_back(i);
        else                right_indices.push_back(i);
    }
    return {left_indices, right_indices};
}

// ===== Step 2: Assign grid positions top-down, then build LayoutNodes =====

struct GridPosition {
    float column;   // fractional column position (center of node)
    int   row;
};

struct NodePlacement {
    core::TreeNode* node;
    GridPosition    grid_pos;
    bool            is_group2;
    bool            is_left_side;
    int             stack_index; // for Group2: 0-based index in the stacked column
};

// Place Group2 attachments for a node at the given row and column,
// updating placements and tracking the max stack depth per row.
static void PlaceGroup2Attachments(
    core::TreeNode* node, float column, int row,
    std::vector<NodePlacement>& placements,
    std::unordered_map<int, int>& row_max_group2_stack)
{
    int attachment_count = (int)node->group2_attachments.size();
    if (attachment_count == 0) return;

    auto [left_indices, right_indices] = DistributeAttachmentSides(attachment_count);

    for (int stack_pos = 0; stack_pos < (int)left_indices.size(); ++stack_pos) {
        core::TreeNode* attachment = node->group2_attachments[left_indices[stack_pos]];
        placements.push_back({attachment, {column - 1.0f, row}, true, true, stack_pos});
    }
    for (int stack_pos = 0; stack_pos < (int)right_indices.size(); ++stack_pos) {
        core::TreeNode* attachment = node->group2_attachments[right_indices[stack_pos]];
        placements.push_back({attachment, {column + 1.0f, row}, true, false, stack_pos});
    }

    int max_side_count = std::max((int)left_indices.size(), (int)right_indices.size());
    auto it = row_max_group2_stack.find(row);
    if (it == row_max_group2_stack.end() || it->second < max_side_count) {
        row_max_group2_stack[row] = max_side_count;
    }
}

// Recursively assign grid positions to nodes in a depth-first traversal.
static void AssignGridPositions(
    core::TreeNode* node,
    float column,
    int row,
    std::vector<NodePlacement>& placements,
    std::unordered_map<int, int>& row_max_group2_stack)
{
    // Place this node at (column, row)
    placements.push_back({node, {column, row}, false, false, 0});

    // Place Group2 attachments (side-attached contextual nodes)
    PlaceGroup2Attachments(node, column, row, placements, row_max_group2_stack);

    // Place Group1 children (structural children in the row below)
    auto& children = node->group1_children;
    if (children.empty()) return;

    int child_row = row + 1;

    if (children.size() == 1) {
        AssignGridPositions(children[0], column, child_row, placements, row_max_group2_stack);
    } else if (children.size() % 2 == 1) {
        // Odd children: anchor middle child under parent, spread halves outward.
        int mid = (int)children.size() / 2;
        float half_mid_width = (float)children[mid]->subtree_width / 2.0f;

        // Center child
        AssignGridPositions(children[mid], column, child_row, placements, row_max_group2_stack);

        // Left half: grow leftward from middle
        float cursor = column - half_mid_width;
        for (int i = mid - 1; i >= 0; --i) {
            cursor -= (float)(children[i]->right_overhang + children[i + 1]->left_overhang);
            float child_col = cursor - (float)children[i]->subtree_width / 2.0f;
            AssignGridPositions(children[i], child_col, child_row, placements, row_max_group2_stack);
            cursor -= (float)children[i]->subtree_width;
        }

        // Right half: grow rightward from middle
        cursor = column + half_mid_width;
        for (int i = mid + 1; i < (int)children.size(); ++i) {
            cursor += (float)(children[i - 1]->right_overhang + children[i]->left_overhang);
            float child_col = cursor + (float)children[i]->subtree_width / 2.0f;
            AssignGridPositions(children[i], child_col, child_row, placements, row_max_group2_stack);
            cursor += (float)children[i]->subtree_width;
        }
    } else {
        // Even children: distribute symmetrically using total width.
        float total_width = 0.0f;
        for (int i = 0; i < (int)children.size(); ++i) {
            total_width += (float)children[i]->subtree_width;
            if (i > 0) {
                total_width += (float)(children[i - 1]->right_overhang
                                     + children[i]->left_overhang);
            }
        }

        float cursor = column - total_width / 2.0f;
        for (int i = 0; i < (int)children.size(); ++i) {
            if (i > 0) {
                cursor += (float)(children[i - 1]->right_overhang
                                + children[i]->left_overhang);
            }
            float child_col = cursor + (float)children[i]->subtree_width / 2.0f;
            AssignGridPositions(children[i], child_col, child_row, placements, row_max_group2_stack);
            cursor += (float)children[i]->subtree_width;
        }
    }
}

// ===== Main tree-based layout =====
std::vector<LayoutNode> LayoutEngine::ComputeLayout(const core::AssuranceTree& tree) {
    std::vector<LayoutNode> result;
    if (!tree.root) return result;

    // Step 1: Compute subtree widths and Group2 overhang (bottom-up)
    compute_subtree_info(tree.root);
    for (auto* orphan : tree.orphans) {
        compute_subtree_info(orphan);
    }

    // Step 2: Assign grid positions (top-down)
    std::vector<NodePlacement> placements;
    std::unordered_map<int, int> row_max_group2_stack;

    float root_column = (float)tree.root->subtree_width / 2.0f;
    AssignGridPositions(tree.root, root_column, 0, placements, row_max_group2_stack);

    // Place orphans to the right of the main tree
    float orphan_column = (float)tree.root->subtree_width + 1.0f;
    for (auto* orphan : tree.orphans) {
        AssignGridPositions(orphan, orphan_column, 0, placements, row_max_group2_stack);
        orphan_column += (float)orphan->subtree_width + 1.0f;
    }

    // Step 3: Compute per-node pixel sizes and track max height per row
    int max_row = 0;
    for (const auto& placement : placements) {
        if (placement.grid_pos.row > max_row) max_row = placement.grid_pos.row;
    }

    std::unordered_map<std::string, ImVec2> node_sizes;
    std::vector<float> row_max_height(max_row + 1, kNodeHeight);

    for (const auto& placement : placements) {
        ImVec2 node_size = ComputeNodeSize(placement.node->label, placement.node->role);
        node_sizes[placement.node->id] = node_size;
        if (!placement.is_group2 && node_size.y > row_max_height[placement.grid_pos.row]) {
            row_max_height[placement.grid_pos.row] = node_size.y;
        }
    }

    // Step 4: Compute row Y positions (variable height from Group2 stacking and tall nodes)
    std::vector<float> row_y(max_row + 1, 0.0f);
    float cumulative_y = kTopMargin;
    for (int row = 0; row <= max_row; ++row) {
        row_y[row] = cumulative_y;
        int group2_stack_count = 1;
        auto stack_it = row_max_group2_stack.find(row);
        if (stack_it != row_max_group2_stack.end()) {
            group2_stack_count = std::max(group2_stack_count, stack_it->second);
        }
        float row_height = std::max(row_max_height[row],
                                    (float)group2_stack_count * (kNodeHeight + kSideGap) - kSideGap);
        cumulative_y += row_height + kVSpacing;
    }

    // Step 5: Convert grid positions to pixel positions
    float column_unit = kNodeWidth + kHSpacing;

    for (const auto& placement : placements) {
        LayoutNode layout_node;
        layout_node.id    = placement.node->id;
        layout_node.role  = to_ui_role(placement.node->role);
        layout_node.group = (placement.node->group == core::ElementGroup::Group1)
                          ? ElementGroup::Group1 : ElementGroup::Group2;
        layout_node.label = placement.node->label;
        layout_node.label_secondary = placement.node->label_secondary;
        layout_node.undeveloped = placement.node->undeveloped;
        layout_node.size  = node_sizes[placement.node->id];
        layout_node.parent_id = placement.node->parent ? placement.node->parent->id : "";
        layout_node.is_left_side = placement.is_left_side;
        layout_node.side_stack_index = placement.stack_index;

        float x = kLeftMargin + placement.grid_pos.column * column_unit;
        // Center solution circles within the column
        bool is_solution = (placement.node->role == core::NodeRole::Solution);
        if (is_solution) {
            x += (kNodeWidth - layout_node.size.x) * 0.5f;
        }
        float y = row_y[placement.grid_pos.row];

        if (placement.is_group2) {
            // Stack Group2 nodes vertically from the base row position
            y += (float)placement.stack_index * (kNodeHeight + kSideGap);
        }

        layout_node.position = ImVec2(x, y);
        result.push_back(layout_node);
    }

    return result;
}

// ===== Legacy flat layout (deprecated â€” kept for backwards compatibility) =====
std::vector<LayoutNode> LayoutEngine::ComputeLayout(const std::vector<CanvasElement>& elements) {
    std::vector<LayoutNode> nodes;
    const ImVec2 default_size = ImVec2(kNodeWidth, kNodeHeight);

    // Separate claims (top row) from everything else
    std::vector<CanvasElement> claims;
    std::vector<CanvasElement> others;
    for (const auto& element : elements) {
        if (element.role == ElementRole::Claim) claims.push_back(element);
        else others.push_back(element);
    }

    // Place claims in the first row
    for (size_t i = 0; i < claims.size(); ++i) {
        LayoutNode layout_node;
        layout_node.id = claims[i].id;
        layout_node.role = claims[i].role;
        layout_node.label = claims[i].label;
        layout_node.label_secondary = claims[i].label_secondary;
        layout_node.undeveloped = claims[i].undeveloped;
        layout_node.size = default_size;
        layout_node.position = ImVec2(kLeftMargin + (float)i * (kNodeWidth + kHSpacing), kTopMargin);
        layout_node.parent_id = claims[i].parent_id;
        nodes.push_back(layout_node);
    }

    // Place remaining element types in subsequent rows, grouped by role
    auto place_role_row = [&](ElementRole target_role, size_t row_index, const std::vector<CanvasElement>& pool) {
        std::vector<CanvasElement> row_elements;
        for (const auto& element : pool) {
            if (element.role == target_role) row_elements.push_back(element);
        }
        for (size_t i = 0; i < row_elements.size(); ++i) {
            LayoutNode layout_node;
            layout_node.id = row_elements[i].id;
            layout_node.role = row_elements[i].role;
            layout_node.label = row_elements[i].label;
            layout_node.label_secondary = row_elements[i].label_secondary;
            layout_node.undeveloped = row_elements[i].undeveloped;
            layout_node.size = default_size;
            layout_node.position = ImVec2(kLeftMargin + (float)i * (kNodeWidth + kHSpacing),
                                          kTopMargin + (float)row_index * (kNodeHeight + kVSpacing));
            layout_node.parent_id = row_elements[i].parent_id;
            nodes.push_back(layout_node);
        }
    };

    std::vector<ElementRole> role_order = {
        ElementRole::Strategy, ElementRole::SubClaim, ElementRole::Solution,
        ElementRole::Evidence, ElementRole::Context,  ElementRole::Justification,
        ElementRole::Assumption, ElementRole::Other
    };
    for (size_t row = 0; row < role_order.size(); ++row) {
        place_role_row(role_order[row], 1 + row, others);
    }

    return nodes;
}

} // namespace ui::gsn
