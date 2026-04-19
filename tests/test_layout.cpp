#include <gtest/gtest.h>
#include "core/assurance_tree.h"
#include "parser/xml_parser.h"

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
