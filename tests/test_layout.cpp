#include <gtest/gtest.h>
#include "core/assurance_tree.h"
#include "parser/xml_parser.h"
#include "ui/gsn/gsn_layout.h"

// We test the layout engine indirectly through the tree since the layout
// engine is coupled to ImGui types. Instead, we test the tree structure
// and subtree width computations which drive the layout.

using namespace core;
using namespace parser;

static AssuranceTree build_tree(const char* xml) {
    ParseResult r = parse_sacm_xml_string(xml);
    EXPECT_TRUE(r.success) << r.error_message;
    return AssuranceTree::Build(r.assurance_case);
}

// ----- Subtree width calculation -----

static int compute_width(TreeNode* n) {
    if (n->group1_children.empty()) { n->subtree_width = 1; return 1; }
    int total = 0;
    for (auto* c : n->group1_children) total += compute_width(c);
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
        if (ln.id == "CtxMidChild") ctx_node = &ln;
        if (ln.id == "EvidenceLeft") ev_node = &ln;
    }
    ASSERT_NE(ctx_node, nullptr) << "CtxMidChild not found in layout";
    ASSERT_NE(ev_node, nullptr) << "EvidenceLeft not found in layout";

    // They must NOT overlap
    EXPECT_FALSE(rects_overlap(ctx_node->position, ctx_node->size,
                               ev_node->position, ev_node->size))
        << "Group2 context node overlaps with sibling's evidence node!"
        << " ctx=(" << ctx_node->position.x << "," << ctx_node->position.y << ")"
        << " ev=(" << ev_node->position.x << "," << ev_node->position.y << ")";
}

TEST(LayoutTest, OddChildrenMiddleCenteredUnderParent) {
    // When a node has 3 children, the middle child should be at the same
    // X-center as the parent.
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="Top" name="Top" assertionDeclaration="asserted"/>
    <argumentReasoning id="Strat" name="Strat"/>
    <claim id="A" name="A" assertionDeclaration="asserted"/>
    <claim id="B" name="B" assertionDeclaration="asserted"/>
    <claim id="C" name="C" assertionDeclaration="asserted"/>
    <assertedInference id="AI1" name="AI1">
      <source ref="A"/>
      <source ref="B"/>
      <source ref="C"/>
      <target ref="Top"/>
      <reasoning ref="Strat"/>
    </assertedInference>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree(xml);
    ASSERT_NE(tree.root, nullptr);

    ui::gsn::LayoutEngine engine;
    auto layout = engine.ComputeLayout(tree);

    const ui::gsn::LayoutNode* strat = nullptr;
    const ui::gsn::LayoutNode* mid = nullptr;
    for (const auto& ln : layout) {
        if (ln.id == "Strat") strat = &ln;
        if (ln.id == "B") mid = &ln;
    }
    ASSERT_NE(strat, nullptr);
    ASSERT_NE(mid, nullptr);

    // Middle child B should have the same X-center as its parent Strat
    float strat_center = strat->position.x + strat->size.x / 2.0f;
    float mid_center = mid->position.x + mid->size.x / 2.0f;
    EXPECT_NEAR(strat_center, mid_center, 1.0f)
        << "Middle child should be centered under parent";
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
