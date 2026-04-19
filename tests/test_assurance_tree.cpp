#include <gtest/gtest.h>
#include "core/assurance_tree.h"
#include "parser/xml_parser.h"

using namespace core;
using namespace parser;

// Helper: parse XML string and build tree
static AssuranceTree build_tree_from_xml(const char* xml) {
    ParseResult r = parse_sacm_xml_string(xml);
    EXPECT_TRUE(r.success) << r.error_message;
    return AssuranceTree::Build(r.assurance_case);
}

// ----- Root identification -----

TEST(AssuranceTreeTest, RootIsTopClaim) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="TopClaim" assertionDeclaration="asserted"/>
    <claim id="cl_sub" name="SubClaim" assertionDeclaration="asserted"/>
    <assertedInference id="inf_1" name="Inf1">
      <source ref="cl_sub"/>
      <target ref="cl_top"/>
    </assertedInference>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree_from_xml(xml);
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->id, "cl_top");
    EXPECT_EQ(tree.root->role, NodeRole::Claim);
}

// ----- Group 1 children via AssertedInference -----

TEST(AssuranceTreeTest, InferenceWiresGroup1Children) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <claim id="cl_a" name="SubA" assertionDeclaration="asserted"/>
    <claim id="cl_b" name="SubB" assertionDeclaration="asserted"/>
    <assertedInference id="inf_1" name="Inf1">
      <source ref="cl_a"/>
      <source ref="cl_b"/>
      <target ref="cl_top"/>
    </assertedInference>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree_from_xml(xml);
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->group1_children.size(), 2);
    EXPECT_EQ(tree.root->group1_children[0]->id, "cl_a");
    EXPECT_EQ(tree.root->group1_children[1]->id, "cl_b");
}

// ----- Reasoning inserts Strategy between parent and children -----

TEST(AssuranceTreeTest, ReasoningInsertsStrategy) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <claim id="cl_sub" name="Sub" assertionDeclaration="asserted"/>
    <argumentReasoning id="ar_1" name="Strategy1"/>
    <assertedInference id="inf_1" name="Inf1" reasoning="ar_1">
      <source ref="cl_sub"/>
      <target ref="cl_top"/>
    </assertedInference>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree_from_xml(xml);
    ASSERT_NE(tree.root, nullptr);
    // Top should have Strategy as child
    ASSERT_EQ(tree.root->group1_children.size(), 1);
    TreeNode* strategy = tree.root->group1_children[0];
    EXPECT_EQ(strategy->id, "ar_1");
    EXPECT_EQ(strategy->role, NodeRole::Strategy);
    // Strategy should have sub-claim as child
    ASSERT_EQ(strategy->group1_children.size(), 1);
    EXPECT_EQ(strategy->group1_children[0]->id, "cl_sub");
}

// ----- AssertedContext wires Group 2 attachment -----

TEST(AssuranceTreeTest, ContextIsGroup2Attachment) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <artifactReference id="ctx_1" name="Context1"/>
    <assertedContext id="acx_1" name="Ctx1">
      <source ref="ctx_1"/>
      <target ref="cl_top"/>
    </assertedContext>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree_from_xml(xml);
    ASSERT_NE(tree.root, nullptr);
    ASSERT_EQ(tree.root->group2_attachments.size(), 1);
    EXPECT_EQ(tree.root->group2_attachments[0]->id, "ctx_1");
    EXPECT_EQ(tree.root->group2_attachments[0]->role, NodeRole::Context);
    EXPECT_EQ(tree.root->group2_attachments[0]->group, ElementGroup::Group2);
}

// ----- AssertedEvidence wires Group 1 Solution leaf -----

TEST(AssuranceTreeTest, EvidenceIsSolutionGroup1Leaf) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <artifactReference id="ev_1" name="Evidence1"/>
    <assertedEvidence id="ae_1" name="Ev1">
      <source ref="ev_1"/>
      <target ref="cl_top"/>
    </assertedEvidence>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree_from_xml(xml);
    ASSERT_NE(tree.root, nullptr);
    // Evidence should be Group 1 child (Solution)
    ASSERT_EQ(tree.root->group1_children.size(), 1);
    EXPECT_EQ(tree.root->group1_children[0]->id, "ev_1");
    EXPECT_EQ(tree.root->group1_children[0]->role, NodeRole::Solution);
    EXPECT_EQ(tree.root->group1_children[0]->group, ElementGroup::Group1);
}

// ----- Assumed claim becomes Assumption (Group 2) -----

TEST(AssuranceTreeTest, AssumedClaimIsAssumption) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <claim id="cl_assumed" name="Assumed" assertionDeclaration="assumed"/>
    <assertedInference id="inf_1" name="Inf1">
      <source ref="cl_assumed"/>
      <target ref="cl_top"/>
    </assertedInference>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree_from_xml(xml);
    ASSERT_NE(tree.root, nullptr);
    // Assumed claims are wired via AssertedInference as Group 1 children currently
    // (since AssertedInference always creates Group 1 children)
    // The role is Assumption though
    ASSERT_GE(tree.root->group1_children.size(), 1);
    TreeNode* assumed = tree.root->group1_children[0];
    EXPECT_EQ(assumed->id, "cl_assumed");
    EXPECT_EQ(assumed->role, NodeRole::Assumption);
}

// ----- Orphaned elements -----

TEST(AssuranceTreeTest, OrphanedElementsAtRootLevel) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="urn:test" id="T" name="T">
  <argumentPackage id="AP" name="AP">
    <claim id="cl_top" name="Top" assertionDeclaration="asserted"/>
    <artifactReference id="orphan_1" name="Orphan"/>
  </argumentPackage>
</sacm:AssuranceCasePackage>)";

    auto tree = build_tree_from_xml(xml);
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->id, "cl_top");
    // orphan_1 should be in orphans list
    ASSERT_EQ(tree.orphans.size(), 1);
    EXPECT_EQ(tree.orphans[0]->id, "orphan_1");
}

// ----- Full example from sacm_example_core_argument.xml -----

TEST(AssuranceTreeTest, FullVehicleBrakingExample) {
    const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<SACM:AssuranceCasePackage xmlns:SACM="http://example.org/sacm/2.3"
    id="acp_vehicle_braking" name="VehicleBrakingAssuranceCase">
  <argumentPackage id="argpkg_braking" name="BrakingArgumentPackage">
    <Claim id="cl_top" name="TopClaim" assertionDeclaration="asserted"/>
    <Claim id="cl_sub_1" name="SubClaim_Architecture" assertionDeclaration="asserted"/>
    <Claim id="cl_sub_2" name="SubClaim_Verification" assertionDeclaration="needsSupport"/>
    <Claim id="cl_assumption_1" name="Assumption_Environment" assertionDeclaration="assumed"/>
    <ArgumentReasoning id="ar_1" name="Reasoning_ArchitectureAndVerification"/>
    <ArtifactReference id="ctx_1" name="OperationalDesignDomainContext"/>
    <ArtifactReference id="ev_1" name="BrakeFMEAReference"/>
    <ArtifactReference id="ev_2" name="BrakeVerificationReportReference"/>
    <AssertedInference id="inf_1" name="Inference_ToTop" reasoning="ar_1">
      <source ref="cl_sub_1"/>
      <source ref="cl_sub_2"/>
      <target ref="cl_top"/>
    </AssertedInference>
    <AssertedInference id="inf_2" name="Inference_AssumptionToArchitecture">
      <source ref="cl_assumption_1"/>
      <target ref="cl_sub_1"/>
    </AssertedInference>
    <AssertedContext id="acx_1" name="Context_ForTopClaim">
      <source ref="ctx_1"/>
      <target ref="cl_top"/>
    </AssertedContext>
    <AssertedEvidence id="ae_1" name="Evidence_ForArchitectureClaim">
      <source ref="ev_1"/>
      <target ref="cl_sub_1"/>
    </AssertedEvidence>
    <AssertedEvidence id="ae_2" name="Evidence_ForVerificationClaim">
      <source ref="ev_2"/>
      <target ref="cl_sub_2"/>
    </AssertedEvidence>
  </argumentPackage>
</SACM:AssuranceCasePackage>)";

    auto tree = build_tree_from_xml(xml);
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->id, "cl_top");

    // Top claim should have:
    //   Group 1: ar_1 (Strategy, with cl_sub_1 and cl_sub_2 as children)
    //   Group 2: ctx_1 (Context)
    EXPECT_EQ(tree.root->group1_children.size(), 1); // ar_1
    EXPECT_EQ(tree.root->group2_attachments.size(), 1); // ctx_1

    TreeNode* strategy = tree.root->group1_children[0];
    EXPECT_EQ(strategy->id, "ar_1");
    EXPECT_EQ(strategy->role, NodeRole::Strategy);

    // Strategy should have cl_sub_1 and cl_sub_2
    EXPECT_EQ(strategy->group1_children.size(), 2);

    // Find cl_sub_1 and cl_sub_2 under strategy
    TreeNode* sub1 = nullptr;
    TreeNode* sub2 = nullptr;
    for (auto* c : strategy->group1_children) {
        if (c->id == "cl_sub_1") sub1 = c;
        if (c->id == "cl_sub_2") sub2 = c;
    }
    ASSERT_NE(sub1, nullptr);
    ASSERT_NE(sub2, nullptr);

    // cl_sub_1 should have:
    //   Group 1: cl_assumption_1 (Assumption wired via inference), ev_1 (Solution)
    EXPECT_GE(sub1->group1_children.size(), 2);

    // cl_sub_2 should have ev_2 (Solution)
    EXPECT_EQ(sub2->group1_children.size(), 1);
    EXPECT_EQ(sub2->group1_children[0]->id, "ev_2");
    EXPECT_EQ(sub2->group1_children[0]->role, NodeRole::Solution);

    // ctx_1 should be context attachment on root
    EXPECT_EQ(tree.root->group2_attachments[0]->id, "ctx_1");
    EXPECT_EQ(tree.root->group2_attachments[0]->role, NodeRole::Context);
}
