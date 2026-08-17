#include "core/assurance_tree.h"
#include "core/gsn_layout.h"
#include "imgui.h"
#include "imgui_internal.h" // ImGuiContext::TreeNodeStack, to check the tree-depth cap holds
#include "parser/xml_parser.h"
#include "ui/gsn/gsn_dpi.h"
#include "ui/gsn/gsn_layout.h"
#include "ui/tree_view.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <unordered_map>

// We test the layout engine indirectly through the tree since the layout
// engine is coupled to ImGui types. Instead, we test the tree structure
// and subtree width computations which drive the layout.

using namespace core;
using namespace parser;

static float scaled_size(float reference_pixels) {
    return ui::gsn::DpiSize(reference_pixels);
}

class ScopedImGuiFrame {
public:
    ScopedImGuiFrame() : previous_(ImGui::GetCurrentContext()) {
        context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().DisplaySize = ImVec2(1200.0f, 800.0f);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
    }

    ~ScopedImGuiFrame() {
        ImGui::EndFrame();
        ImGui::DestroyContext(context_);
        if (previous_) {
            ImGui::SetCurrentContext(previous_);
        }
    }

private:
    ImGuiContext* previous_ = nullptr;
    ImGuiContext* context_ = nullptr;
};

static TreeNode* add_layout_node(
    AssuranceTree& tree, const std::string& id, NodeRole role, ElementGroup group, const std::string& label) {
    auto node = std::make_unique<TreeNode>();
    node->id = id;
    node->role = role;
    node->group = group;
    node->label = label;
    TreeNode* raw_node = node.get();
    tree.nodes.push_back(std::move(node));
    return raw_node;
}

static std::string long_layout_label(const std::string& id) {
    return id + ": Long label\n"
                "This element contains a detailed safety argument explanation that needs enough "
                "room to wrap cleanly after HiDPI font scaling. The layout should widen the node "
                "before allowing it to become a tall narrow shape, because narrow GSN elements are "
                "hard to read and make nearby canvas elements overlap. The text is intentionally "
                "long so the measured bounds exercise both horizontal growth and vertical growth.";
}

static AssuranceTree build_tree(const char* xml) {
    auto r = parse_sacm_xml_string(xml);
    EXPECT_TRUE(r.has_value()) << (r ? "" : r.error());
    return AssuranceTree::Build(*r);
}

// ----- Subtree width calculation -----

static int compute_width(TreeNode* n) {
    if (n->group1_children.empty()) {
        n->subtree_width = 1;
        return 1;
    }
    int total = 0;
    for (auto* c : n->group1_children)
        total += compute_width(c);
    n->subtree_width = total;
    return total;
}

TEST(LayoutTest, LeafNodeHasWidth1) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_1" name="Leaf" assertionDeclaration="asserted"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree(xml);
    ASSERT_NE(tree.root, nullptr);
    compute_width(tree.root);
    EXPECT_EQ(tree.root->subtree_width, 1);
}

TEST(LayoutTest, TwoChildrenWidth2) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <claim id="cl_a" name="A" assertionDeclaration="asserted"/>
    <claim id="cl_b" name="B" assertionDeclaration="asserted"/>
    <assertedInference id="inf_1" name="Inf1">
      <source ref="cl_a"/>
      <source ref="cl_b"/>
      <target ref="cl_top"/>
    </assertedInference>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree(xml);
    ASSERT_NE(tree.root, nullptr);
    compute_width(tree.root);
    EXPECT_EQ(tree.root->subtree_width, 2);
    EXPECT_EQ(tree.root->group1_children[0]->subtree_width, 1);
    EXPECT_EQ(tree.root->group1_children[1]->subtree_width, 1);
}

TEST(LayoutTest, AsymmetricSubtreeWidth) {
    // cl_top has children cl_a (leaf) and cl_b (has 2 children)
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <claim id="cl_a" name="A" assertionDeclaration="asserted"/>
    <claim id="cl_b" name="B" assertionDeclaration="asserted"/>
    <claim id="cl_c" name="C" assertionDeclaration="asserted"/>
    <claim id="cl_d" name="D" assertionDeclaration="asserted"/>
    <assertedInference id="inf_1" name="Inf1">
      <source ref="cl_a"/>
      <source ref="cl_b"/>
      <target ref="cl_top"/>
    </assertedInference>
    <assertedInference id="inf_2" name="Inf2">
      <source ref="cl_c"/>
      <source ref="cl_d"/>
      <target ref="cl_b"/>
    </assertedInference>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree(xml);
    ASSERT_NE(tree.root, nullptr);
    compute_width(tree.root);
    EXPECT_EQ(tree.root->subtree_width, 3); // cl_a(1) + cl_b(2)
}

// ----- Group 2 side distribution -----

TEST(LayoutTest, Group2DistributionPattern) {
    // This tests the distribution logic described in spec §10.2.1
    // 1 → left
    // 2 → left, right
    // 3 → left, left, right
    // 4 → left, left, right, right

    // We verify this through tree structure — count of left vs right
    // is determined by (n+1)/2 left, n/2 right

    EXPECT_EQ((1 + 1) / 2, 1); // 1 att: 1 left, 0 right
    EXPECT_EQ((2 + 1) / 2, 1); // 2 att: 1 left, 1 right
    EXPECT_EQ((3 + 1) / 2, 2); // 3 att: 2 left, 1 right
    EXPECT_EQ((4 + 1) / 2, 2); // 4 att: 2 left, 2 right
    EXPECT_EQ((5 + 1) / 2, 3); // 5 att: 3 left, 2 right
}

// ----- Determinism -----

TEST(LayoutTest, DeterministicTreeBuilding) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <claim id="cl_a" name="A" assertionDeclaration="asserted"/>
    <claim id="cl_b" name="B" assertionDeclaration="asserted"/>
    <artifactReference id="ctx_1" name="Context1"/>
    <assertedInference id="inf_1" name="Inf1">
      <source ref="cl_a"/>
      <source ref="cl_b"/>
      <target ref="cl_top"/>
    </assertedInference>
    <assertedContext id="acx_1" name="Ctx1">
      <source ref="ctx_1"/>
      <target ref="cl_top"/>
    </assertedContext>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    // Build tree twice and verify identical structure
    auto tree1 = build_tree(xml);
    auto tree2 = build_tree(xml);

    ASSERT_NE(tree1.root, nullptr);
    ASSERT_NE(tree2.root, nullptr);
    EXPECT_EQ(tree1.root->id, tree2.root->id);
    EXPECT_EQ(tree1.root->group1_children.size(), tree2.root->group1_children.size());
    EXPECT_EQ(tree1.root->group2_attachments.size(), tree2.root->group2_attachments.size());

    for (size_t i = 0; i < tree1.root->group1_children.size(); ++i) {
        EXPECT_EQ(tree1.root->group1_children[i]->id, tree2.root->group1_children[i]->id);
    }
    for (size_t i = 0; i < tree1.root->group2_attachments.size(); ++i) {
        EXPECT_EQ(tree1.root->group2_attachments[i]->id, tree2.root->group2_attachments[i]->id);
    }
}

// ----- Overlap regression: Group2 attachment must not overlap sibling subtrees -----

static bool rects_overlap(ImVec2 pos1, ImVec2 size1, ImVec2 pos2, ImVec2 size2) {
    // Returns true if two axis-aligned rectangles overlap (non-zero area)
    float l1 = pos1.x, r1 = pos1.x + size1.x, t1 = pos1.y, b1 = pos1.y + size1.y;
    float l2 = pos2.x, r2 = pos2.x + size2.x, t2 = pos2.y, b2 = pos2.y + size2.y;
    return l1 < r2 && r1 > l2 && t1 < b2 && b1 > t2;
}

static const ui::gsn::LayoutNode* find_layout_node(const std::vector<ui::gsn::LayoutNode>& layout,
                                                   const std::string& id) {
    for (const auto& layout_node : layout) {
        if (layout_node.id == id)
            return &layout_node;
    }
    return nullptr;
}

static float node_center_y(const ui::gsn::LayoutNode& layout_node) {
    return layout_node.position.y + layout_node.size.y * 0.5f;
}

static float stack_center_y(const ui::gsn::LayoutNode& first, const ui::gsn::LayoutNode& second) {
    const float stack_top = std::min(first.position.y, second.position.y);
    const float stack_bottom = std::max(first.position.y + first.size.y, second.position.y + second.size.y);
    return (stack_top + stack_bottom) * 0.5f;
}

TEST(LayoutTest, LongNonSolutionLabelsGrowHorizontally) {
    ScopedImGuiFrame imgui_frame;

    struct RoleCase {
        NodeRole role;
        ElementGroup group;
        const char* id;
    };

    const RoleCase cases[] = {
        {NodeRole::Claim, ElementGroup::Group1, "Claim"},
        {NodeRole::Strategy, ElementGroup::Group1, "Strategy"},
        {NodeRole::Context, ElementGroup::Group2, "Context"},
        {NodeRole::Assumption, ElementGroup::Group2, "Assumption"},
        {NodeRole::Justification, ElementGroup::Group2, "Justification"},
    };

    for (const auto& role_case : cases) {
        AssuranceTree tree;
        TreeNode* node =
            add_layout_node(tree, role_case.id, role_case.role, role_case.group, long_layout_label(role_case.id));
        tree.root = node;

        ui::gsn::LayoutEngine engine;
        auto layout = engine.ComputeLayout(tree);
        ASSERT_EQ(layout.size(), 1u) << role_case.id;
        EXPECT_GT(layout[0].size.x, scaled_size(260.0f)) << role_case.id;
        EXPECT_GE(layout[0].size.y, scaled_size(100.0f)) << role_case.id;
    }
}

TEST(LayoutTest, NonSolutionElementsUseWiderMinimumWidth) {
    ScopedImGuiFrame imgui_frame;

    struct RoleCase {
        NodeRole role;
        ElementGroup group;
        const char* id;
    };

    const RoleCase cases[] = {
        {NodeRole::Claim, ElementGroup::Group1, "Claim"},
        {NodeRole::Strategy, ElementGroup::Group1, "Strategy"},
        {NodeRole::Context, ElementGroup::Group2, "Context"},
        {NodeRole::Assumption, ElementGroup::Group2, "Assumption"},
        {NodeRole::Justification, ElementGroup::Group2, "Justification"},
    };

    for (const auto& role_case : cases) {
        AssuranceTree tree;
        TreeNode* node = add_layout_node(tree, role_case.id, role_case.role, role_case.group, role_case.id);
        tree.root = node;

        ui::gsn::LayoutEngine engine;
        auto layout = engine.ComputeLayout(tree);
        ASSERT_EQ(layout.size(), 1u) << role_case.id;
        EXPECT_FLOAT_EQ(layout[0].size.x, scaled_size(260.0f)) << role_case.id;
        EXPECT_FLOAT_EQ(layout[0].size.y, scaled_size(100.0f)) << role_case.id;
    }
}

TEST(LayoutTest, SolutionMinimumSizeIsUnchanged) {
    ScopedImGuiFrame imgui_frame;

    AssuranceTree tree;
    TreeNode* node = add_layout_node(tree, "Solution", NodeRole::Solution, ElementGroup::Group1, "Solution");
    tree.root = node;

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);
    ASSERT_EQ(layout.size(), 1u);
    EXPECT_FLOAT_EQ(layout[0].size.x, scaled_size(160.0f));
    EXPECT_FLOAT_EQ(layout[0].size.y, scaled_size(160.0f));
}

// Text box inscribed in a Solution circle of this diameter, mirroring
// ui::gsn::ComputeTextWrapWidth for the Solution role.
static float solution_text_wrap(float diameter) {
    const float radius = diameter * 0.5f;
    const float inset = radius * 0.29f;
    return std::max((radius - inset) * 2.0f - scaled_size(6.0f) * 2.0f, scaled_size(40.0f));
}

// True when the label, wrapped to that box, fits inside the circle's usable
// text height. g_BoldFont is null in tests, so both halves use the same font --
// exactly what the layout engine measures.
static bool solution_label_fits(const std::string& label, float diameter) {
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    const float wrap = solution_text_wrap(diameter);
    const char* start = label.c_str();
    const char* newline = strchr(start, '\n');
    const ImVec2 first = font->CalcTextSizeA(font_size, FLT_MAX, wrap, start, newline ? newline : nullptr);
    ImVec2 rest(0.0f, 0.0f);
    if (newline)
        rest = font->CalcTextSizeA(font_size, FLT_MAX, wrap, newline + 1, nullptr);
    return first.y + rest.y + scaled_size(6.0f) * 2.0f <= diameter * 0.7f;
}

TEST(LayoutTest, SolutionCircleGrowsOnlyAsFarAsItsLabelNeeds) {
    // A circle's text box widens as the circle grows. Measuring the label once
    // at the base diameter counts the lines of a narrow wrap and then sizes a
    // disc tall enough to stack all of them -- roughly twice the diameter the
    // text actually occupies, with a thin ribbon of text across the middle.
    ScopedImGuiFrame imgui_frame;

    AssuranceTree tree;
    const std::string label = long_layout_label("Sn4");
    TreeNode* node = add_layout_node(tree, "Sn4", NodeRole::Solution, ElementGroup::Group1, label);
    tree.root = node;

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);
    ASSERT_EQ(layout.size(), 1u);

    const float diameter = layout[0].size.x;
    EXPECT_FLOAT_EQ(layout[0].size.y, diameter); // circles stay square
    EXPECT_GT(diameter, scaled_size(160.0f));    // it did have to grow
    EXPECT_TRUE(solution_label_fits(label, diameter)) << "label overflows a " << diameter << "px circle";
    // And no further: a meaningfully smaller circle must not fit the label.
    EXPECT_FALSE(solution_label_fits(label, diameter - scaled_size(24.0f)))
        << "circle is oversized: the label still fits " << scaled_size(24.0f) << "px smaller";
}

TEST(LayoutTest, WideNodesAreCenteredWithinExpandedColumns) {
    GsnLayoutInput input;
    GsnLayoutInputNode wide;
    wide.id = "Wide";
    GsnLayoutInputNode narrow;
    narrow.id = "Narrow";
    input.nodes = {wide, narrow};
    input.roots = {"Wide", "Narrow"};

    GsnLayoutOptions options;
    options.margin_x = 20.0;
    options.margin_y = 20.0;
    options.base_node_width = 260.0;
    options.default_node_height = 100.0;
    options.horizontal_spacing = 40.0;

    std::unordered_map<std::string, GsnLayoutSize> sizes;
    sizes["Wide"] = {500.0, 100.0};
    sizes["Narrow"] = {260.0, 100.0};

    const GsnLayoutGraphResult layout = LayoutGsnGraph(input, sizes, options);

    const auto wide_it = std::find_if(
        layout.nodes.begin(), layout.nodes.end(), [](const GsnLayoutNode& node) { return node.id == "Wide"; });
    const auto narrow_it = std::find_if(
        layout.nodes.begin(), layout.nodes.end(), [](const GsnLayoutNode& node) { return node.id == "Narrow"; });
    ASSERT_NE(wide_it, layout.nodes.end());
    ASSERT_NE(narrow_it, layout.nodes.end());
    // The two roots are packed side by side in input order without overlapping
    // (the contour packer no longer snaps roots to uniform column slots).
    EXPECT_LE(wide_it->x + wide_it->width, narrow_it->x + 1.0);
}

TEST(LayoutTest, WideGroup2AttachmentNoOverlapWithParent) {
    ScopedImGuiFrame imgui_frame;

    AssuranceTree tree;
    TreeNode* parent = add_layout_node(tree, "Parent", NodeRole::Claim, ElementGroup::Group1, "Parent: Claim");
    TreeNode* context =
        add_layout_node(tree, "Context", NodeRole::Context, ElementGroup::Group2, long_layout_label("Context"));
    context->parent = parent;
    parent->group2_attachments.push_back(context);
    tree.root = parent;

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);

    const ui::gsn::LayoutNode* parent_node = nullptr;
    const ui::gsn::LayoutNode* context_node = nullptr;
    for (const auto& ln : layout) {
        if (ln.id == "Parent")
            parent_node = &ln;
        if (ln.id == "Context")
            context_node = &ln;
    }
    ASSERT_NE(parent_node, nullptr);
    ASSERT_NE(context_node, nullptr);
    EXPECT_GT(context_node->size.x, scaled_size(220.0f));
    EXPECT_FALSE(rects_overlap(parent_node->position, parent_node->size, context_node->position, context_node->size));
}

TEST(LayoutTest, Group2SideStacksAreCenteredAroundOwner) {
    ScopedImGuiFrame imgui_frame;

    AssuranceTree tree;
    TreeNode* parent = add_layout_node(tree, "Parent", NodeRole::Claim, ElementGroup::Group1, "Parent: Claim");

    struct AttachmentCase {
        const char* id;
        NodeRole role;
    };

    const AttachmentCase attachment_cases[] = {
        {"ContextA", NodeRole::Context},
        {"AssumptionB", NodeRole::Assumption},
        {"JustificationC", NodeRole::Justification},
        {"ContextD", NodeRole::Context},
        {"AssumptionE", NodeRole::Assumption},
    };

    for (const auto& attachment_case : attachment_cases) {
        TreeNode* attachment =
            add_layout_node(tree, attachment_case.id, attachment_case.role, ElementGroup::Group2, attachment_case.id);
        attachment->parent = parent;
        parent->group2_attachments.push_back(attachment);
    }
    tree.root = parent;

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);

    const ui::gsn::LayoutNode* parent_node = find_layout_node(layout, "Parent");
    const ui::gsn::LayoutNode* left_top = find_layout_node(layout, "ContextA");
    const ui::gsn::LayoutNode* left_middle = find_layout_node(layout, "AssumptionB");
    const ui::gsn::LayoutNode* left_bottom = find_layout_node(layout, "JustificationC");
    const ui::gsn::LayoutNode* right_top = find_layout_node(layout, "ContextD");
    const ui::gsn::LayoutNode* right_bottom = find_layout_node(layout, "AssumptionE");

    ASSERT_NE(parent_node, nullptr);
    ASSERT_NE(left_top, nullptr);
    ASSERT_NE(left_middle, nullptr);
    ASSERT_NE(left_bottom, nullptr);
    ASSERT_NE(right_top, nullptr);
    ASSERT_NE(right_bottom, nullptr);

    const float parent_center_y = node_center_y(*parent_node);
    EXPECT_LT(node_center_y(*left_top), parent_center_y);
    EXPECT_NEAR(node_center_y(*left_middle), parent_center_y, 1.0f);
    EXPECT_GT(node_center_y(*left_bottom), parent_center_y);
    EXPECT_NEAR(stack_center_y(*right_top, *right_bottom), parent_center_y, 1.0f);
}

TEST(LayoutTest, CenteredGroup2StackExpandsRowBoundsForChildren) {
    ScopedImGuiFrame imgui_frame;

    AssuranceTree tree;
    TreeNode* parent = add_layout_node(tree, "Parent", NodeRole::Claim, ElementGroup::Group1, "Parent: Claim");
    TreeNode* child = add_layout_node(tree, "Child", NodeRole::Claim, ElementGroup::Group1, "Child: Claim");
    child->parent = parent;
    parent->group1_children.push_back(child);

    for (int attachment_index = 0; attachment_index < 5; ++attachment_index) {
        const std::string id = "Context" + std::to_string(attachment_index);
        TreeNode* attachment = add_layout_node(tree, id, NodeRole::Context, ElementGroup::Group2, id);
        attachment->parent = parent;
        parent->group2_attachments.push_back(attachment);
    }
    tree.root = parent;

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);

    const ui::gsn::LayoutNode* child_node = find_layout_node(layout, "Child");
    ASSERT_NE(child_node, nullptr);

    for (int attachment_index = 0; attachment_index < 5; ++attachment_index) {
        const std::string id = "Context" + std::to_string(attachment_index);
        const ui::gsn::LayoutNode* attachment_node = find_layout_node(layout, id);
        ASSERT_NE(attachment_node, nullptr);
        EXPECT_FALSE(
            rects_overlap(attachment_node->position, attachment_node->size, child_node->position, child_node->size))
            << id << " overlaps the child row";
    }
}

TEST(LayoutTest, Group2AttachmentNoOverlapWithSibling) {
    // Regression test for the CTX_15 / EV_14 overlap bug.
    // Structure: parent has 3 children (odd, so middle is centered).
    //   parent -> strategy -> {left_child, mid_child, right_child}
    //   mid_child -> goal_under_mid
    //   goal_under_mid has a context (Group2, placed at column-1)
    //   left_child -> evidence_under_left (leaf)
    // The context's column must not overlap with evidence_under_left's column.
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="Top" name="Top" assertionDeclaration="asserted"/>
    <argumentReasoning id="Strat" name="Strat"/>
    <claim id="Left" name="Left" assertionDeclaration="asserted"/>
    <claim id="Mid" name="Mid" assertionDeclaration="asserted"/>
    <claim id="Right" name="Right" assertionDeclaration="asserted"/>
    <claim id="GoalUnderMid" name="GoalUnderMid" assertionDeclaration="asserted"/>
    <artifactReference id="EvidenceLeft" name="EvidenceLeft"/>
    <artifactReference id="CtxMidChild" name="CtxMidChild"/>

    <!-- Top -> Strat -> {Left, Mid, Right} -->
    <assertedInference id="AI1" name="AI1">
      <source ref="Left"/>
      <source ref="Mid"/>
      <source ref="Right"/>
      <target ref="Top"/>
      <reasoning ref="Strat"/>
    </assertedInference>

    <!-- Mid -> GoalUnderMid -->
    <assertedInference id="AI2" name="AI2">
      <source ref="GoalUnderMid"/>
      <target ref="Mid"/>
    </assertedInference>

    <!-- GoalUnderMid has CtxMidChild as context (Group2, placed left) -->
    <assertedContext id="AC1" name="AC1">
      <source ref="CtxMidChild"/>
      <target ref="GoalUnderMid"/>
    </assertedContext>

    <!-- Left has EvidenceLeft as evidence (Group1 child) -->
    <assertedEvidence id="AE1" name="AE1">
      <source ref="EvidenceLeft"/>
      <target ref="Left"/>
    </assertedEvidence>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree(xml);
    ASSERT_NE(tree.root, nullptr);

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);

    // Find the nodes we care about
    const ui::gsn::LayoutNode* ctx_node = nullptr;
    const ui::gsn::LayoutNode* ev_node = nullptr;
    for (const auto& ln : layout) {
        if (ln.id == "CtxMidChild")
            ctx_node = &ln;
        if (ln.id == "EvidenceLeft")
            ev_node = &ln;
    }
    ASSERT_NE(ctx_node, nullptr) << "CtxMidChild not found in layout";
    ASSERT_NE(ev_node, nullptr) << "EvidenceLeft not found in layout";

    // They must NOT overlap
    EXPECT_FALSE(rects_overlap(ctx_node->position, ctx_node->size, ev_node->position, ev_node->size))
        << "Group2 context node overlaps with sibling's evidence node!"
        << " ctx=(" << ctx_node->position.x << "," << ctx_node->position.y << ")"
        << " ev=(" << ev_node->position.x << "," << ev_node->position.y << ")";
}

TEST(LayoutTest, AdjacentSiblingsWithFacingGroup2AttachmentsDoNotOverlap) {
    // Regression test: two adjacent siblings each with group2 attachments facing each other
    // caused overlap when both had subtree_width >= 2. The right attachment of the left
    // sibling and the left attachment of the right sibling were placed in nearly identical
    // column slots because overhang was only added for subtree_width < 2.
    //
    // Structure: Top -> Strat -> {Left, Right}
    //   Left  has 3 group1 children (subtree_width=3) + 2 contexts (left/right)
    //   Right has 2 group1 children (subtree_width=2) + 1 context (left)
    //
    // Left's right context and Right's left context must not overlap.
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="Top" name="Top" assertionDeclaration="asserted"/>
    <argumentReasoning id="Strat" name="Strat"/>
    <claim id="Left" name="Left" assertionDeclaration="asserted"/>
    <claim id="Right" name="Right" assertionDeclaration="asserted"/>
    <claim id="L1" name="L1" assertionDeclaration="asserted"/>
    <claim id="L2" name="L2" assertionDeclaration="asserted"/>
    <claim id="L3" name="L3" assertionDeclaration="asserted"/>
    <claim id="R1" name="R1" assertionDeclaration="asserted"/>
    <claim id="R2" name="R2" assertionDeclaration="asserted"/>
    <artifactReference id="CtxLeft1" name="CtxLeft1"/>
    <artifactReference id="CtxLeft2" name="CtxLeft2"/>
    <artifactReference id="CtxRight1" name="CtxRight1"/>
    <assertedInference id="AI0">
      <source ref="Left"/>
      <source ref="Right"/>
      <target ref="Top"/>
      <reasoning ref="Strat"/>
    </assertedInference>
    <assertedInference id="AI1">
      <source ref="L1"/>
      <source ref="L2"/>
      <source ref="L3"/>
      <target ref="Left"/>
    </assertedInference>
    <assertedInference id="AI2">
      <source ref="R1"/>
      <source ref="R2"/>
      <target ref="Right"/>
    </assertedInference>
    <assertedContext id="AC1">
      <source ref="CtxLeft1"/>
      <target ref="Left"/>
    </assertedContext>
    <assertedContext id="AC2">
      <source ref="CtxLeft2"/>
      <target ref="Left"/>
    </assertedContext>
    <assertedContext id="AC3">
      <source ref="CtxRight1"/>
      <target ref="Right"/>
    </assertedContext>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree(xml);
    ASSERT_NE(tree.root, nullptr);

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);

    const ui::gsn::LayoutNode* ctx_left2 = find_layout_node(layout, "CtxLeft2");
    const ui::gsn::LayoutNode* ctx_right1 = find_layout_node(layout, "CtxRight1");
    ASSERT_NE(ctx_left2, nullptr) << "CtxLeft2 not found in layout";
    ASSERT_NE(ctx_right1, nullptr) << "CtxRight1 not found in layout";

    EXPECT_FALSE(rects_overlap(ctx_left2->position, ctx_left2->size, ctx_right1->position, ctx_right1->size))
        << "Right attachment of Left sibling overlaps left attachment of Right sibling!"
        << " CtxLeft2=(" << ctx_left2->position.x << "," << ctx_left2->position.y << " " << ctx_left2->size.x << "x"
        << ctx_left2->size.y << ")"
        << " CtxRight1=(" << ctx_right1->position.x << "," << ctx_right1->position.y << " " << ctx_right1->size.x << "x"
        << ctx_right1->size.y << ")";
}

TEST(LayoutTest, AsymmetricOddChildrenUseCompactSpan) {
    ScopedImGuiFrame imgui_frame;

    AssuranceTree tree;
    TreeNode* root = add_layout_node(tree, "Root", NodeRole::Claim, ElementGroup::Group1, "Root");
    TreeNode* left = add_layout_node(tree, "Left", NodeRole::Claim, ElementGroup::Group1, "Left");
    TreeNode* middle = add_layout_node(tree, "Middle", NodeRole::Claim, ElementGroup::Group1, "Middle");
    TreeNode* right = add_layout_node(tree, "Right", NodeRole::Claim, ElementGroup::Group1, "Right");

    left->parent = root;
    middle->parent = root;
    right->parent = root;
    root->group1_children = {left, middle, right};

    for (int leaf_index = 0; leaf_index < 5; ++leaf_index) {
        TreeNode* leaf = add_layout_node(
            tree, "LeftLeaf" + std::to_string(leaf_index), NodeRole::Claim, ElementGroup::Group1, "Left leaf");
        leaf->parent = left;
        left->group1_children.push_back(leaf);
    }
    tree.root = root;

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);

    const ui::gsn::LayoutNode* root_node = find_layout_node(layout, "Root");
    const ui::gsn::LayoutNode* left_node = find_layout_node(layout, "Left");
    const ui::gsn::LayoutNode* middle_node = find_layout_node(layout, "Middle");
    const ui::gsn::LayoutNode* right_node = find_layout_node(layout, "Right");
    ASSERT_NE(root_node, nullptr);
    ASSERT_NE(left_node, nullptr);
    ASSERT_NE(middle_node, nullptr);
    ASSERT_NE(right_node, nullptr);

    auto center_x = [](const ui::gsn::LayoutNode* n) { return n->position.x + n->size.x / 2.0f; };
    // The parent is centred over the span of its children even when the left
    // child carries a much wider subtree, and the children stay ordered without
    // overlapping (compact contour packing).
    EXPECT_GE(center_x(root_node), center_x(left_node) - 1.0f);
    EXPECT_LE(center_x(root_node), center_x(right_node) + 1.0f);
    EXPECT_LT(left_node->position.x + left_node->size.x, middle_node->position.x + 1.0f);
    EXPECT_LT(middle_node->position.x + middle_node->size.x, right_node->position.x + 1.0f);
}

TEST(LayoutTest, UndevelopedFlagPropagatesToLayoutNode) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="Top" name="Top" undeveloped="true" assertionDeclaration="asserted"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree(xml);
    ASSERT_NE(tree.root, nullptr);

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);
    ASSERT_EQ(layout.size(), 1u);
    EXPECT_EQ(layout[0].id, "Top");
    EXPECT_TRUE(layout[0].undeveloped);
}

TEST(LayoutTest, UninstantiatedFlagPropagatesToLayoutNode) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="Top" name="Top" isAbstract="true" assertionDeclaration="asserted"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree(xml);
    ASSERT_NE(tree.root, nullptr);

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);
    ASSERT_EQ(layout.size(), 1u);
    EXPECT_EQ(layout[0].id, "Top");
    EXPECT_TRUE(layout[0].uninstantiated);
}

// Builds a deep parent->child chain via AssertedInference relationships (source = child,
// target = parent), i.e. the structure that produces a very deep AssuranceTree.
static std::string make_deep_chain_xml(int chain_length) {
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
)";
    for (int i = 0; i < chain_length; ++i)
        xml += "    <claim id=\"cl_" + std::to_string(i) + "\" name=\"C" + std::to_string(i) +
               "\" assertionDeclaration=\"asserted\"/>\n";
    for (int i = 0; i + 1 < chain_length; ++i)
        xml += "    <assertedInference id=\"inf_" + std::to_string(i) + "\" name=\"I" + std::to_string(i) +
               "\">\n      <source ref=\"cl_" + std::to_string(i + 1) + "\"/>\n      <target ref=\"cl_" +
               std::to_string(i) + "\"/>\n    </assertedInference>\n";
    xml += "  </argumentPackage>\n</sacm:AssuranceCasePackage>";
    return xml;
}

TEST(LayoutTest, DeepRelationshipChainThroughRealLayoutPath) {
    // Reproduce the real app path: a deep chain built into an AssuranceTree and laid out through
    // ui::gsn::LayoutEngine::ComputeLayout(tree) - exactly what RebuildDerivedViewsIfNeeded runs.
    constexpr int kChainLength = 12000;
    const std::string xml = make_deep_chain_xml(kChainLength);

    ScopedImGuiFrame imgui_frame;
    AssuranceTree tree = AssuranceTree::Build(*parse_sacm_xml_string(xml));
    ASSERT_NE(tree.root, nullptr);

    ui::gsn::LayoutEngine engine;
    const std::vector<ui::gsn::LayoutNode> layout = engine.ComputeLayout(tree);
    EXPECT_EQ(layout.size(), static_cast<size_t>(kChainLength));
}

TEST(LayoutTest, DeepChainTreeViewRendersWithoutStackOverflow) {
    // The tree-view panel renders the (default-expanded) hierarchy every frame. A recursive
    // renderer overflowed the call stack on a deep chain, silently terminating the app while the
    // project was still "loading". Rendering is now iterative, so it must render without crashing.
    constexpr int kChainLength = 12000;
    const std::string xml = make_deep_chain_xml(kChainLength);

    ScopedImGuiFrame imgui_frame;
    AssuranceTree tree = AssuranceTree::Build(*parse_sacm_xml_string(xml));
    ASSERT_NE(tree.root, nullptr);

    ImGui::SetNextWindowSize(ImVec2(400.0f, 600.0f));
    ImGui::Begin("##tree_view_test");
    ui::ElementContextActions actions;
    ui::ShowTreeViewPanel(&tree, nullptr, ui::GetUiState(), actions, nullptr);
    ImGui::End();

    SUCCEED();
}

TEST(LayoutTest, DeepChainTreeViewStopsNestingAtImGuiTreeDepthLimit) {
    // ImGui indexes a 32-bit per-depth mask with `1 << window->DC.TreeDepth` on a signed int. At
    // depth 32 the shift is undefined and, on x86-64 and AArch64, aliases onto depth 0 instead of
    // vanishing, so TreePop pops an ImGuiTreeNodeStackData belonging to another node
    // (ocornut/imgui#9509). The navigator therefore stops handing ImGui new tree levels at depth
    // 31 and renders deeper nodes flat.
    //
    // Tree lines are what make the defect observable: stack data is only recorded when they are
    // enabled, and ImGui's own TreePop assert then fires on the mismatch. With the cap in place
    // g.TreeNodeStack must balance back to empty. The chain is deeper than the cap but short
    // enough to keep the test fast -- the property is about depth 32, not about 12,000 nodes.
    constexpr int kChainLength = 64;
    const std::string xml = make_deep_chain_xml(kChainLength);

    ScopedImGuiFrame imgui_frame;
    ImGui::GetStyle().TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesFull;

    AssuranceTree tree = AssuranceTree::Build(*parse_sacm_xml_string(xml));
    ASSERT_NE(tree.root, nullptr);

    ImGui::SetNextWindowSize(ImVec2(400.0f, 600.0f));
    ImGui::Begin("##tree_view_depth_cap");
    ui::ElementContextActions actions;
    ui::ShowTreeViewPanel(&tree, nullptr, ui::GetUiState(), actions, nullptr);
    ImGui::End();

    // Every ImGuiTreeNodeStackData pushed during the frame was popped by its own TreePop. Without
    // the cap this is non-zero: the aliased depth-0 bit is cleared early, so the root's entry is
    // never popped.
    EXPECT_EQ(ImGui::GetCurrentContext()->TreeNodeStack.Size, 0);
}

TEST(LayoutTest, DeepLinearChainDoesNotOverflowStack) {
    // A long parent->child chain previously recursed once per link in the layout traversal,
    // overflowing the call stack (~10k deep on Windows) and silently terminating the app. The
    // traversal is now iterative, so a very deep chain must lay out without crashing.
    constexpr int kChainLength = 20000;

    GsnLayoutInput input;
    input.nodes.reserve(kChainLength);
    for (int i = 0; i < kChainLength; ++i) {
        GsnLayoutInputNode node;
        node.id = "n" + std::to_string(i);
        node.role = NodeRole::Claim;
        if (i > 0)
            node.parent_id = "n" + std::to_string(i - 1);
        if (i + 1 < kChainLength)
            node.group1_children.push_back("n" + std::to_string(i + 1));
        input.nodes.push_back(std::move(node));
    }
    input.roots = {"n0"};

    const std::unordered_map<std::string, GsnLayoutSize> sizes;
    const GsnLayoutGraphResult layout = LayoutGsnGraph(input, sizes);

    EXPECT_EQ(layout.nodes.size(), static_cast<size_t>(kChainLength));
    EXPECT_TRUE(layout.warnings.empty());

    const auto first =
        std::find_if(layout.nodes.begin(), layout.nodes.end(), [](const GsnLayoutNode& n) { return n.id == "n0"; });
    const std::string last_id = "n" + std::to_string(kChainLength - 1);
    const auto last =
        std::find_if(layout.nodes.begin(), layout.nodes.end(), [&](const GsnLayoutNode& n) { return n.id == last_id; });
    ASSERT_NE(first, layout.nodes.end());
    ASSERT_NE(last, layout.nodes.end());
    // The chain descends one row per link, so the last node must sit far below the first.
    EXPECT_GT(last->y, first->y);
}
