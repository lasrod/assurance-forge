#include <gtest/gtest.h>

#include "core/element_factory.h"
#include "core/assurance_tree.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <algorithm>

namespace {

// Build a minimal AssuranceCase + matching SACM package containing one Goal
// (G1) so subsequent factory calls have a parent to attach to.
struct MiniCase {
    parser::AssuranceCase ac;
    sacm::AssuranceCasePackage pkg;
};

MiniCase MakeRootGoalCase() {
    MiniCase mc;
    parser::SacmElement g;
    g.id = "G1";
    g.type = "claim";
    g.name = "Top Goal";
    mc.ac.elements.push_back(g);

    sacm::Claim c;
    c.id = "G1";
    c.name = "Top Goal";
    sacm::ArgumentPackage ap;
    ap.claims.push_back(c);
    mc.pkg.argumentPackages.push_back(ap);
    return mc;
}

bool ParserHasId(const parser::AssuranceCase& ac, const std::string& id) {
    for (const auto& e : ac.elements) if (e.id == id) return true;
    return false;
}

bool SacmHasClaim(const sacm::AssuranceCasePackage& pkg, const std::string& id) {
    for (const auto& ap : pkg.argumentPackages)
        for (const auto& c : ap.claims) if (c.id == id) return true;
    return false;
}

}  // namespace

TEST(ElementFactoryRemove, RemoveLeafElement) {
    auto mc = MakeRootGoalCase();

    // Add a Solution under G1.
    std::string new_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1",
                                      core::NewElementKind::Solution, new_id, err))
        << err;
    ASSERT_FALSE(new_id.empty());
    ASSERT_TRUE(ParserHasId(mc.ac, new_id));

    // Solution is a leaf: 0 descendants.
    EXPECT_EQ(core::CountDescendants(mc.ac, new_id), 0);

    // Remove without cascade succeeds.
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, new_id, /*cascade=*/false, err)) << err;

    // Solution gone from parser model.
    EXPECT_FALSE(ParserHasId(mc.ac, new_id));

    // The originating AssertedEvidence relationship is also gone (it had the
    // removed element as its only source).
    for (const auto& e : mc.ac.elements) {
        EXPECT_NE(e.type, "assertedevidence")
            << "Dangling assertedevidence relationship left behind: " << e.id;
    }

    // SACM model: artifactReferences and assertedEvidences both empty for the package.
    ASSERT_EQ(mc.pkg.argumentPackages.size(), 1u);
    EXPECT_TRUE(mc.pkg.argumentPackages[0].artifactReferences.empty());
    EXPECT_TRUE(mc.pkg.argumentPackages[0].assertedEvidences.empty());
}

TEST(ElementFactoryRemove, RemoveElementWithChildrenCascade) {
    auto mc = MakeRootGoalCase();

    // Add a Strategy under G1, then a Goal under the strategy.
    std::string strategy_id, leaf_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1",
                                      core::NewElementKind::Strategy, strategy_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1",
                                      core::NewElementKind::Goal, leaf_id, err)) << err;
    // leaf_id was added under G1 directly, not under the strategy. Add another
    // goal under the strategy so we have a real subtree to remove.
    std::string sub_goal_id;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, strategy_id,
                                      core::NewElementKind::Goal, sub_goal_id, err)) << err;

    // The strategy now has at least one descendant.
    EXPECT_GT(core::CountDescendants(mc.ac, strategy_id), 0);

    // Cannot remove without cascade.
    EXPECT_FALSE(core::RemoveElement(mc.ac, &mc.pkg, strategy_id, /*cascade=*/false, err));
    EXPECT_FALSE(err.empty());

    // Cascade remove succeeds.
    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, strategy_id, /*cascade=*/true, err)) << err;

    // Strategy and its sub-goal are gone from both models.
    EXPECT_FALSE(ParserHasId(mc.ac, strategy_id));
    EXPECT_FALSE(ParserHasId(mc.ac, sub_goal_id));
    EXPECT_FALSE(SacmHasClaim(mc.pkg, sub_goal_id));

    // The unrelated leaf goal added directly under G1 survives.
    EXPECT_TRUE(ParserHasId(mc.ac, leaf_id));
    EXPECT_TRUE(SacmHasClaim(mc.pkg, leaf_id));

    // Original root goal also still present.
    EXPECT_TRUE(ParserHasId(mc.ac, "G1"));
}

TEST(ElementFactoryRemove, RemoveUnknownIdReturnsError) {
    auto mc = MakeRootGoalCase();
    std::string err;
    EXPECT_FALSE(core::RemoveElement(mc.ac, &mc.pkg, "DOES_NOT_EXIST",
                                     /*cascade=*/false, err));
    EXPECT_FALSE(err.empty());
}

// Real GSN/SACM XML wires multiple sub-goals under a strategy through a single
// AssertedInference whose reasoning is the strategy and whose sources are the
// sub-goals. Removing one sub-goal must NOT delete the inference, the strategy
// or the sibling sub-goals.
TEST(ElementFactoryRemove, RemoveSourceOfSharedInferencePreservesSiblings) {
    parser::AssuranceCase ac;
    sacm::AssuranceCasePackage pkg;

    auto add_node = [&](const char* id, const char* type) {
        parser::SacmElement e;
        e.id = id;
        e.type = type;
        e.name = id;
        ac.elements.push_back(e);
    };
    add_node("CL_TOP", "claim");
    add_node("AR_1",   "argumentreasoning");  // strategy
    add_node("CL_A",   "claim");
    add_node("CL_B",   "claim");

    parser::SacmElement inference;
    inference.id = "INF_1";
    inference.type = "assertedinference";
    inference.target_refs.push_back("CL_TOP");
    inference.reasoning_ref = "AR_1";
    inference.source_refs = {"CL_A", "CL_B"};
    ac.elements.push_back(inference);

    sacm::ArgumentPackage ap;
    sacm::Claim ct; ct.id = "CL_TOP"; ap.claims.push_back(ct);
    sacm::Claim ca; ca.id = "CL_A";   ap.claims.push_back(ca);
    sacm::Claim cb; cb.id = "CL_B";   ap.claims.push_back(cb);
    sacm::ArgumentReasoning ar; ar.id = "AR_1"; ap.argumentReasonings.push_back(ar);
    sacm::AssertedInference inf;
    inf.id = "INF_1";
    inf.targets = {"CL_TOP"};
    inf.sources = {"CL_A", "CL_B"};
    inf.reasoning = "AR_1";
    ap.assertedInferences.push_back(inf);
    pkg.argumentPackages.push_back(ap);

    // Sub-goal CL_A is a leaf.
    EXPECT_EQ(core::CountDescendants(ac, "CL_A"), 0);

    std::string err;
    ASSERT_TRUE(core::RemoveElement(ac, &pkg, "CL_A", /*cascade=*/false, err)) << err;

    // CL_A is gone, but the strategy, sibling, target and inference remain.
    EXPECT_FALSE(ParserHasId(ac, "CL_A"));
    EXPECT_TRUE(ParserHasId(ac, "AR_1"));
    EXPECT_TRUE(ParserHasId(ac, "CL_B"));
    EXPECT_TRUE(ParserHasId(ac, "CL_TOP"));
    EXPECT_TRUE(ParserHasId(ac, "INF_1"));

    // Inference's source list no longer contains CL_A.
    for (const auto& e : ac.elements) {
        if (e.id != "INF_1") continue;
        EXPECT_EQ(e.source_refs.size(), 1u);
        EXPECT_EQ(e.source_refs[0], "CL_B");
        EXPECT_EQ(e.reasoning_ref, "AR_1");
    }

    // SACM model: same survival on the inference, claim CL_A removed.
    ASSERT_EQ(pkg.argumentPackages.size(), 1u);
    const auto& got_ap = pkg.argumentPackages[0];
    EXPECT_FALSE(SacmHasClaim(pkg, "CL_A"));
    EXPECT_TRUE(SacmHasClaim(pkg, "CL_B"));
    ASSERT_EQ(got_ap.assertedInferences.size(), 1u);
    EXPECT_EQ(got_ap.assertedInferences[0].sources.size(), 1u);
    EXPECT_EQ(got_ap.assertedInferences[0].sources[0], "CL_B");
    EXPECT_EQ(got_ap.assertedInferences[0].reasoning, "AR_1");
}

// Reproduces user-reported bug: a strategy with a single sub-goal underneath.
// Removing the sub-goal must leave the strategy intact.
TEST(ElementFactoryRemove, RemoveSingleSubGoalUnderStrategyKeepsStrategy) {
    auto mc = MakeRootGoalCase();

    std::string strategy_id, sub_goal_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1",
                                      core::NewElementKind::Strategy, strategy_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, strategy_id,
                                      core::NewElementKind::Goal, sub_goal_id, err)) << err;

    EXPECT_EQ(core::CountDescendants(mc.ac, sub_goal_id), 0);

    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, sub_goal_id, /*cascade=*/false, err)) << err;

    EXPECT_FALSE(ParserHasId(mc.ac, sub_goal_id));
    EXPECT_TRUE(ParserHasId(mc.ac, strategy_id))
        << "Strategy must survive removal of its only sub-goal.";
    EXPECT_TRUE(ParserHasId(mc.ac, "G1"));

    // The inference wiring strategy under G1 must still exist (target=G1,
    // reasoning=strategy_id) so the tree continues to render the strategy.
    bool found_wiring = false;
    for (const auto& e : mc.ac.elements) {
        if (e.type != "assertedinference") continue;
        bool targets_g1 = std::find(e.target_refs.begin(), e.target_refs.end(),
                                    std::string("G1")) != e.target_refs.end();
        if (targets_g1 && e.reasoning_ref == strategy_id) {
            found_wiring = true;
            break;
        }
    }
    EXPECT_TRUE(found_wiring) << "Inference wiring strategy under G1 was dropped.";

    // And the rebuilt assurance tree must still contain the strategy as a child of G1.
    auto tree = core::AssuranceTree::Build(mc.ac);
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->id, "G1");
    bool strategy_in_tree = false;
    for (const auto* child : tree.root->group1_children) {
        if (child->id == strategy_id && child->role == core::NodeRole::Strategy) {
            strategy_in_tree = true;
        }
    }
    EXPECT_TRUE(strategy_in_tree)
        << "Tree builder dropped the strategy after sub-goal removal.";
}
