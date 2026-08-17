#include "ui/gsn/gsn_layout.h"

#include "core/gsn_layout.h"
#include "ui/gsn/gsn_canvas.h" // for g_BoldFont
#include "ui/gsn/gsn_dpi.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace ui::gsn {

// Bold font pointer shared between layout engine and node drawing code.
// Set by main.cpp at startup.
ImFont* g_BoldFont = nullptr;

// ===== Layout constants =====
static constexpr float kNodeWidth = 260.0f;     // default node width (px)
static constexpr float kNodeHeight = 100.0f;    // default node height (px)
static constexpr float kSolutionWidth = 160.0f; // circle diameter for Solution/Evidence nodes
static constexpr float kSolutionHeight = 160.0f;
static constexpr float kHSpacing = 40.0f;   // horizontal gap between adjacent columns
static constexpr float kVSpacing = 80.0f;   // vertical gap between adjacent rows
static constexpr float kLeftMargin = 20.0f; // canvas left margin
static constexpr float kTopMargin = 20.0f;  // canvas top margin
static constexpr float kSideGap = 20.0f;    // vertical gap between stacked Group2 nodes

// Text measurement constants (must match gsn_canvas.cpp drawing insets)
static constexpr float kTextPadding = 6.0f;        // inner padding between shape edge and text
static constexpr float kMinTextWrap = 40.0f;       // minimum text wrap width
static constexpr float kParallelogramSkew = 0.15f; // fraction of width for Strategy shape inset
static constexpr float kCircleTextInset = 0.29f;   // fraction of radius for circle text area
static constexpr float kStadiumTextInset = 0.15f;  // fraction of height for stadium text inset
static constexpr float kCircleTextRatio = 0.7f;    // effective text height as fraction of circle diameter
static constexpr float kNodeWidthGrowthStep = 20.0f;
static constexpr float kMaxNodeWidth = 460.0f;
static constexpr float kMaxStadiumWidth = 520.0f;
static constexpr float kSolutionDiameterTolerance = 1.0f; // bisection precision for the circle diameter
static constexpr int kSolutionGrowthDoublings = 8;        // bound on the search for a diameter that fits

static bool is_stadium_role(core::NodeRole role) {
    return role == core::NodeRole::Context || role == core::NodeRole::Assumption ||
           role == core::NodeRole::Justification;
}

static float max_width_for_role(core::NodeRole role) {
    return is_stadium_role(role) ? kMaxStadiumWidth : kMaxNodeWidth;
}

static float preferred_height_ratio_for_role(core::NodeRole role) {
    if (is_stadium_role(role))
        return 0.45f;
    if (role == core::NodeRole::Strategy)
        return 0.55f;
    return 0.65f;
}

static float ComputeTextWrapWidth(core::NodeRole role, float width, float height) {
    float text_padding = DpiSize(kTextPadding);
    float text_wrap = width - text_padding * 2.0f;
    if (role == core::NodeRole::Strategy) {
        float skew = width * kParallelogramSkew;
        text_wrap = width - skew * 2.0f - text_padding * 2.0f;
    } else if (role == core::NodeRole::Solution) {
        float radius = width * 0.5f;
        float inset = radius * kCircleTextInset;
        text_wrap = (radius - inset) * 2.0f - text_padding * 2.0f;
    } else if (is_stadium_role(role)) {
        float inset = height * kStadiumTextInset;
        text_wrap = width - inset * 2.0f - text_padding * 2.0f;
    }
    return std::max(text_wrap, DpiSize(kMinTextWrap));
}

static float
MeasureLabelHeight(const std::string& label, float font_size, float text_wrap, ImFont* bold_font, ImFont* normal_font) {
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
    return bold_text_size.y + rest_text_size.y;
}

static float ComputeRequiredHeight(const std::string& label,
                                   core::NodeRole role,
                                   float width,
                                   float base_height,
                                   float font_size,
                                   ImFont* bold_font,
                                   ImFont* normal_font) {
    float height = base_height;
    for (int i = 0; i < 6; ++i) {
        float text_wrap = ComputeTextWrapWidth(role, width, height);
        float required_height =
            MeasureLabelHeight(label, font_size, text_wrap, bold_font, normal_font) + DpiSize(kTextPadding) * 2.0f;
        float next_height = std::max(base_height, required_height);
        if (std::fabs(next_height - height) < 0.5f)
            return next_height;
        height = next_height;
    }
    return height;
}

// True when the label, wrapped to the text box inscribed in a circle of this
// diameter, fits inside that box.
static bool
SolutionLabelFits(const std::string& label, float diameter, float font_size, ImFont* bold_font, ImFont* normal_font) {
    float text_wrap = ComputeTextWrapWidth(core::NodeRole::Solution, diameter, diameter);
    float required_height =
        MeasureLabelHeight(label, font_size, text_wrap, bold_font, normal_font) + DpiSize(kTextPadding) * 2.0f;
    return required_height <= diameter * kCircleTextRatio;
}

// Smallest diameter at or above `base_diameter` whose inscribed text box holds
// the label.
//
// A circle's text box widens as the circle grows, so the label has to be
// re-measured at every candidate diameter. Measuring it once at the base
// diameter counts the lines of a narrow wrap and then sizes a circle wide
// enough to stack all of them -- which is how a solution whose text fits in a
// 280px disc ended up drawn as a 550px one with a thin ribbon of text across
// the middle. The required height never grows with the diameter, so "fits"
// flips exactly once and bisection finds the crossing.
static float ComputeSolutionDiameter(
    const std::string& label, float base_diameter, float font_size, ImFont* bold_font, ImFont* normal_font) {
    if (SolutionLabelFits(label, base_diameter, font_size, bold_font, normal_font))
        return base_diameter;

    float low = base_diameter;
    float high = base_diameter * 2.0f;
    for (int i = 0; i < kSolutionGrowthDoublings; ++i) {
        if (SolutionLabelFits(label, high, font_size, bold_font, normal_font))
            break;
        low = high;
        high *= 2.0f;
    }

    const float tolerance = DpiSize(kSolutionDiameterTolerance);
    while (high - low > tolerance) {
        float mid = (low + high) * 0.5f;
        if (SolutionLabelFits(label, mid, font_size, bold_font, normal_font))
            high = mid;
        else
            low = mid;
    }
    return high;
}

// ===== Compute node size based on text content =====
// Measures text using ImGui font metrics and returns the required node dimensions.
// Nodes grow horizontally before becoming tall so wrapped text keeps a readable shape.
static ImVec2 ComputeNodeSize(const std::string& label, core::NodeRole role) {
    bool is_solution = (role == core::NodeRole::Solution);
    float base_width = DpiSize(is_solution ? kSolutionWidth : kNodeWidth);
    float base_height = DpiSize(is_solution ? kSolutionHeight : kNodeHeight);

    // If no ImGui context (e.g. in unit tests), use base size
    if (!ImGui::GetCurrentContext() || !ImGui::GetFont()) {
        return ImVec2(base_width, base_height);
    }

    float font_size = ImGui::GetFontSize();
    ImFont* bold_font = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal_font = ImGui::GetFont();

    if (is_solution) {
        // Circles stay square, and grow only as far as the wrapped label needs.
        float diameter =
            ComputeSolutionDiameter(label, std::max(base_width, base_height), font_size, bold_font, normal_font);
        return ImVec2(diameter, diameter);
    }

    float final_width = base_width;
    float final_height =
        ComputeRequiredHeight(label, role, final_width, base_height, font_size, bold_font, normal_font);
    const float max_width = DpiSize(max_width_for_role(role));
    while (final_width < max_width) {
        float preferred_max_height = std::max(base_height, final_width * preferred_height_ratio_for_role(role));
        if (final_height <= preferred_max_height)
            break;

        final_width = std::min(max_width, final_width + DpiSize(kNodeWidthGrowthStep));
        final_height = ComputeRequiredHeight(label, role, final_width, base_height, font_size, bold_font, normal_font);
    }

    return ImVec2(final_width, final_height);
}

// ===== Helper: map core roles to UI roles =====
static ElementRole to_ui_role(core::NodeRole r) {
    switch (r) {
    case core::NodeRole::Claim:
        return ElementRole::Claim;
    case core::NodeRole::Strategy:
        return ElementRole::Strategy;
    case core::NodeRole::Solution:
        return ElementRole::Solution;
    case core::NodeRole::Context:
        return ElementRole::Context;
    case core::NodeRole::Assumption:
        return ElementRole::Assumption;
    case core::NodeRole::Justification:
        return ElementRole::Justification;
    default:
        return ElementRole::Other;
    }
}

// ===== Tree → layout conversion helpers =====

namespace {

LayoutNode ConvertPlacedNode(const core::GsnLayoutNode& placed, const core::TreeNode* tn) {
    LayoutNode ln;
    ln.id = placed.id;
    ln.role = to_ui_role(placed.role);
    ln.group = (placed.group == core::ElementGroup::Group1) ? ElementGroup::Group1 : ElementGroup::Group2;
    ln.label = placed.label;
    ln.label_secondary = placed.label_secondary;
    ln.undeveloped = placed.undeveloped;
    ln.uninstantiated = placed.uninstantiated;
    ln.size = ImVec2(static_cast<float>(placed.width), static_cast<float>(placed.height));
    ln.parent_id = placed.parent_id;
    ln.is_left_side = placed.is_left_side;
    ln.side_stack_index = placed.side_stack_index;
    ln.child_edge_drop = placed.child_edge_drop;
    ln.position = ImVec2(static_cast<float>(placed.x), static_cast<float>(placed.y));
    if (tn) {
        ln.is_counter_source = tn->is_counter_source;
        ln.challenge_target_id = tn->challenge_target_id;
        ln.challenge_target_is_relationship = tn->challenge_target_is_relationship;
        ln.challenge_relationship_id = tn->challenge_relationship_id;
        ln.challenge_anchor_id = tn->challenge_anchor_id;
    }
    return ln;
}

core::GsnLayoutInputNode MakeInputNode(const core::TreeNode& tn) {
    core::GsnLayoutInputNode node;
    node.id = tn.id;
    node.role = tn.role;
    node.group = tn.group;
    node.label = tn.label;
    node.label_secondary = tn.label_secondary;
    node.undeveloped = tn.undeveloped;
    node.uninstantiated = tn.uninstantiated;
    // Counters are cluster roots reached only via their host's challenge_children,
    // so they never carry a structural parent in the layout input.
    node.parent_id = (tn.parent && !tn.is_counter_source) ? tn.parent->id : "";
    for (const core::TreeNode* child : tn.group1_children)
        node.group1_children.push_back(child->id);
    for (const core::TreeNode* attachment : tn.group2_attachments)
        node.group2_attachments.push_back(attachment->id);
    return node;
}

} // namespace

// ===== Main tree-based layout =====
//
// A single LayoutGsnGraph pass lays out the structural tree AND every dialectic
// challenge cluster. Each counter is attached to its host node's
// `challenge_children` with a side (Left/Right/Below); the engine reserves space
// for it in the host's footprint, so challenges never overlap the argument.
std::vector<LayoutNode> LayoutEngine::ComputeLayout(const core::AssuranceTree& tree) {
    std::vector<LayoutNode> result;
    if (tree.nodes.empty())
        return result;

    core::GsnLayoutOptions options;
    options.margin_x = DpiSize(kLeftMargin);
    options.margin_y = DpiSize(kTopMargin);
    options.base_node_width = DpiSize(kNodeWidth);
    options.default_node_height = DpiSize(kNodeHeight);
    options.horizontal_spacing = DpiSize(kHSpacing);
    options.vertical_spacing = DpiSize(kVSpacing);
    options.side_stack_gap = DpiSize(kSideGap);

    // Size every node from the label that is actually rendered, index nodes by
    // id, and build one layout-input node per tree node.
    const ui::UiState& ui_state = ui::GetUiState();
    std::unordered_map<std::string, core::GsnLayoutSize> node_sizes;
    std::unordered_map<std::string, const core::TreeNode*> tree_node_by_id;
    std::unordered_map<std::string, std::size_t> input_index_by_id;
    node_sizes.reserve(tree.nodes.size());
    tree_node_by_id.reserve(tree.nodes.size());

    core::GsnLayoutInput input;
    input.nodes.reserve(tree.nodes.size());
    for (const auto& owned_node : tree.nodes) {
        const core::TreeNode& tree_node = *owned_node;
        tree_node_by_id[tree_node.id] = &tree_node;
        const bool use_secondary = ui_state.show_secondary_language && !tree_node.label_secondary.empty();
        const std::string& active_label = use_secondary ? tree_node.label_secondary : tree_node.label;
        const ImVec2 node_size = ComputeNodeSize(active_label, tree_node.role);
        node_sizes[tree_node.id] = {node_size.x, node_size.y};
        input_index_by_id[tree_node.id] = input.nodes.size();
        input.nodes.push_back(MakeInputNode(tree_node));
    }

    // Roots / orphans exclude counters (they are reached via their host).
    if (tree.root)
        input.roots.push_back(tree.root->id);
    for (const core::TreeNode* orphan : tree.orphans)
        if (!orphan->is_counter_source)
            input.orphans.push_back(orphan->id);

    // Attach each counter to its host's challenge_children with its placement.
    for (const auto& owned_node : tree.nodes) {
        const core::TreeNode& counter = *owned_node;
        if (!counter.is_counter_source || counter.challenge_anchor_id.empty())
            continue;
        const auto host_it = input_index_by_id.find(counter.challenge_anchor_id);
        if (host_it == input_index_by_id.end())
            continue;
        input.nodes[host_it->second].challenge_children.push_back({counter.id, counter.challenge_side});
    }

    const core::GsnLayoutGraphResult layout = core::LayoutGsnGraph(input, node_sizes, options);
    result.reserve(layout.nodes.size());
    for (const core::GsnLayoutNode& placed : layout.nodes) {
        const auto it = tree_node_by_id.find(placed.id);
        result.push_back(ConvertPlacedNode(placed, it != tree_node_by_id.end() ? it->second : nullptr));
    }
    return result;
}

// ===== Legacy flat layout (deprecated â€” kept for backwards compatibility) =====
std::vector<LayoutNode> LayoutEngine::ComputeLayout(const std::vector<CanvasElement>& elements) {
    std::vector<LayoutNode> nodes;
    const float node_width = DpiSize(kNodeWidth);
    const float node_height = DpiSize(kNodeHeight);
    const float h_spacing = DpiSize(kHSpacing);
    const float v_spacing = DpiSize(kVSpacing);
    const float left_margin = DpiSize(kLeftMargin);
    const float top_margin = DpiSize(kTopMargin);
    const ImVec2 default_size = ImVec2(node_width, node_height);

    // Separate claims (top row) from everything else
    std::vector<CanvasElement> claims;
    std::vector<CanvasElement> others;
    for (const auto& element : elements) {
        if (element.role == ElementRole::Claim)
            claims.push_back(element);
        else
            others.push_back(element);
    }

    // Place claims in the first row
    for (size_t i = 0; i < claims.size(); ++i) {
        LayoutNode layout_node;
        layout_node.id = claims[i].id;
        layout_node.role = claims[i].role;
        layout_node.label = claims[i].label;
        layout_node.label_secondary = claims[i].label_secondary;
        layout_node.undeveloped = claims[i].undeveloped;
        layout_node.uninstantiated = claims[i].uninstantiated;
        layout_node.size = default_size;
        layout_node.position = ImVec2(left_margin + (float)i * (node_width + h_spacing), top_margin);
        layout_node.parent_id = claims[i].parent_id;
        nodes.push_back(layout_node);
    }

    // Place remaining element types in subsequent rows, grouped by role
    auto place_role_row = [&](ElementRole target_role, size_t row_index, const std::vector<CanvasElement>& pool) {
        std::vector<CanvasElement> row_elements;
        for (const auto& element : pool) {
            if (element.role == target_role)
                row_elements.push_back(element);
        }
        for (size_t i = 0; i < row_elements.size(); ++i) {
            LayoutNode layout_node;
            layout_node.id = row_elements[i].id;
            layout_node.role = row_elements[i].role;
            layout_node.label = row_elements[i].label;
            layout_node.label_secondary = row_elements[i].label_secondary;
            layout_node.undeveloped = row_elements[i].undeveloped;
            layout_node.uninstantiated = row_elements[i].uninstantiated;
            layout_node.size = default_size;
            layout_node.position = ImVec2(left_margin + (float)i * (node_width + h_spacing),
                                          top_margin + (float)row_index * (node_height + v_spacing));
            layout_node.parent_id = row_elements[i].parent_id;
            nodes.push_back(layout_node);
        }
    };

    std::vector<ElementRole> role_order = {ElementRole::Strategy,
                                           ElementRole::SubClaim,
                                           ElementRole::Solution,
                                           ElementRole::Evidence,
                                           ElementRole::Context,
                                           ElementRole::Justification,
                                           ElementRole::Assumption,
                                           ElementRole::Other};
    for (size_t row = 0; row < role_order.size(); ++row) {
        place_role_row(role_order[row], 1 + row, others);
    }

    return nodes;
}

} // namespace ui::gsn
