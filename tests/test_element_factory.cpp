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

    // Remove using NodeOnly (single-element plan).
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, new_id,
                                    core::RemoveMode::NodeOnly, err)) << err;

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

    // NodeAndDescendants cleanly removes the strategy and its sub-goal.
    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, strategy_id,
                                    core::RemoveMode::NodeAndDescendants, err)) << err;

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
                                     core::RemoveMode::NodeOnly, err));
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
    ASSERT_TRUE(core::RemoveElement(ac, &pkg, "CL_A",
                                    core::RemoveMode::NodeOnly, err)) << err;

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
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, sub_goal_id,
                                    core::RemoveMode::NodeOnly, err)) << err;

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

// NodeOnly remove of a strategy with structural children must reparent the
// children to the strategy's parent (clearing reasoning of the inference).
TEST(ElementFactoryRemove, RemoveNodeOnly_ReparentsStructuralChildrenToParent) {
    auto mc = MakeRootGoalCase();

    std::string strategy_id, sub_goal_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1",
                                      core::NewElementKind::Strategy, strategy_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, strategy_id,
                                      core::NewElementKind::Goal, sub_goal_id, err)) << err;

    // Plan: NodeOnly on a strategy includes only the strategy itself.
    auto plan = core::PlanRemoval(mc.ac, strategy_id, core::RemoveMode::NodeOnly);
    EXPECT_EQ(plan.size(), 1u);
    EXPECT_TRUE(plan.count(strategy_id));

    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, strategy_id,
                                    core::RemoveMode::NodeOnly, err)) << err;

    EXPECT_FALSE(ParserHasId(mc.ac, strategy_id));
    EXPECT_TRUE(ParserHasId(mc.ac, sub_goal_id))
        << "Sub-goal must survive a NodeOnly remove of its parent strategy.";

    auto tree = core::AssuranceTree::Build(mc.ac);
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->id, "G1");
    bool sub_goal_under_g1 = false;
    for (const auto* c : tree.root->group1_children) {
        if (c->id == sub_goal_id) sub_goal_under_g1 = true;
    }
    EXPECT_TRUE(sub_goal_under_g1)
        << "Sub-goal must be reparented to G1 after the strategy is removed.";
}

// NodeOnly remove of a regular sub-claim that has its own children must promote
// those children to the grandparent (rewriting the inference target).
TEST(ElementFactoryRemove, RemoveNodeOnly_PromotesGrandchildrenToGrandparent) {
    auto mc = MakeRootGoalCase();

    std::string g2_id, g3_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1",
                                      core::NewElementKind::Goal, g2_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, g2_id,
                                      core::NewElementKind::Goal, g3_id, err)) << err;

    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, g2_id,
                                    core::RemoveMode::NodeOnly, err)) << err;

    EXPECT_FALSE(ParserHasId(mc.ac, g2_id));
    EXPECT_TRUE(ParserHasId(mc.ac, g3_id));

    auto tree = core::AssuranceTree::Build(mc.ac);
    ASSERT_NE(tree.root, nullptr);
    bool g3_under_g1 = false;
    for (const auto* c : tree.root->group1_children) {
        if (c->id == g3_id) g3_under_g1 = true;
    }
    EXPECT_TRUE(g3_under_g1) << "G3 must be reparented to G1 after G2 is removed.";
}

// NodeOnly must also pull in Group2 attachments (Context/Assumption/Justification)
// of the removed node — they cannot survive without their target.
TEST(ElementFactoryRemove, RemoveNodeOnly_AlsoRemovesGroup2Attachments) {
    auto mc = MakeRootGoalCase();

    std::string sub_goal_id, ctx_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1",
                                      core::NewElementKind::Goal, sub_goal_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, sub_goal_id,
                                      core::NewElementKind::Context, ctx_id, err)) << err;

    auto plan = core::PlanRemoval(mc.ac, sub_goal_id, core::RemoveMode::NodeOnly);
    EXPECT_EQ(plan.size(), 2u);
    EXPECT_TRUE(plan.count(sub_goal_id));
    EXPECT_TRUE(plan.count(ctx_id));

    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, sub_goal_id,
                                    core::RemoveMode::NodeOnly, err)) << err;
    EXPECT_FALSE(ParserHasId(mc.ac, sub_goal_id));
    EXPECT_FALSE(ParserHasId(mc.ac, ctx_id))
        << "Context attachment must go away with its target node.";
}

// Comprehensive regression for the canonical shared-inference shape (matches
// data/sacm_example_core_argument.xml: cl_top / ar_1 / [cl_sub_1, cl_sub_2]
// wired by a single inference inf_1). Exercises both remove modes from every
// relevant vantage point so a sub-claim's removal never disturbs the strategy
// or its other children unexpectedly.
TEST(ElementFactoryRemove, StrategyShape_AllRemoveModes) {
    parser::AssuranceCase ac;
    sacm::AssuranceCasePackage pkg;

    auto add_node = [&](const char* id, const char* type) {
        parser::SacmElement e;
        e.id = id; e.type = type; e.name = id;
        ac.elements.push_back(e);
    };
    add_node("CL_TOP",   "claim");
    add_node("AR_1",     "argumentreasoning");
    add_node("CL_SUB_1", "claim");
    add_node("CL_SUB_2", "claim");

    parser::SacmElement inf;
    inf.id = "INF_1"; inf.type = "assertedinference";
    inf.target_refs = {"CL_TOP"};
    inf.reasoning_ref = "AR_1";
    inf.source_refs = {"CL_SUB_1", "CL_SUB_2"};
    ac.elements.push_back(inf);

    // Mirror into the SACM package so RemoveElement can scrub both models.
    pkg.argumentPackages.emplace_back();
    auto& ap = pkg.argumentPackages.back();
    ap.id = "AP1";
    sacm::Claim c1; c1.id = "CL_TOP";   ap.claims.push_back(c1);
    sacm::Claim c2; c2.id = "CL_SUB_1"; ap.claims.push_back(c2);
    sacm::Claim c3; c3.id = "CL_SUB_2"; ap.claims.push_back(c3);
    sacm::ArgumentReasoning ar; ar.id = "AR_1"; ap.argumentReasonings.push_back(ar);
    sacm::AssertedInference si;
    si.id = "INF_1"; si.targets = {"CL_TOP"}; si.reasoning = "AR_1";
    si.sources = {"CL_SUB_1", "CL_SUB_2"};
    ap.assertedInferences.push_back(si);

    // --- 1. Plan size from a sub-claim --------------------------------------
    auto plan_node = core::PlanRemoval(ac, "CL_SUB_1", core::RemoveMode::NodeOnly);
    auto plan_desc = core::PlanRemoval(ac, "CL_SUB_1", core::RemoveMode::NodeAndDescendants);

    EXPECT_EQ(plan_node.size(), 1u) << "NodeOnly on a leaf sub-claim removes just itself.";
    EXPECT_TRUE(plan_node.count("CL_SUB_1"));

    EXPECT_EQ(plan_desc.size(), 1u) << "Sub-claim has no descendants here.";
    EXPECT_TRUE(plan_desc.count("CL_SUB_1"));
    EXPECT_FALSE(plan_desc.count("AR_1"));
    EXPECT_FALSE(plan_desc.count("CL_TOP"));

    // --- 2. Plan size from the strategy itself ------------------------------
    auto plan_strat_node = core::PlanRemoval(ac, "AR_1", core::RemoveMode::NodeOnly);
    auto plan_strat_desc = core::PlanRemoval(ac, "AR_1", core::RemoveMode::NodeAndDescendants);

    EXPECT_EQ(plan_strat_node.size(), 1u);
    EXPECT_TRUE(plan_strat_node.count("AR_1"));
    EXPECT_FALSE(plan_strat_node.count("CL_TOP"));

    EXPECT_EQ(plan_strat_desc.size(), 3u) << "Strategy + its two sub-claims.";
    EXPECT_TRUE(plan_strat_desc.count("AR_1"));
    EXPECT_TRUE(plan_strat_desc.count("CL_SUB_1"));
    EXPECT_TRUE(plan_strat_desc.count("CL_SUB_2"));
    EXPECT_FALSE(plan_strat_desc.count("CL_TOP"));

    // --- 3. Removing a sub-claim with NodeOnly: AR_1 + CL_TOP survive.
    {
        auto ac2 = ac; auto pkg2 = pkg; std::string err;
        ASSERT_TRUE(core::RemoveElement(ac2, &pkg2, "CL_SUB_1",
                                        core::RemoveMode::NodeOnly, err)) << err;
        EXPECT_FALSE(ParserHasId(ac2, "CL_SUB_1"));
        EXPECT_TRUE(ParserHasId(ac2, "CL_SUB_2"));
        EXPECT_TRUE(ParserHasId(ac2, "AR_1"));
        EXPECT_TRUE(ParserHasId(ac2, "CL_TOP"));
    }
    // --- 4. NodeAndDescendants on the strategy: strategy + sub-claims gone, top stays.
    {
        auto ac2 = ac; auto pkg2 = pkg; std::string err;
        ASSERT_TRUE(core::RemoveElement(ac2, &pkg2, "AR_1",
                                        core::RemoveMode::NodeAndDescendants, err)) << err;
        EXPECT_FALSE(ParserHasId(ac2, "AR_1"));
        EXPECT_FALSE(ParserHasId(ac2, "CL_SUB_1"));
        EXPECT_FALSE(ParserHasId(ac2, "CL_SUB_2"));
        EXPECT_TRUE(ParserHasId(ac2, "CL_TOP"));
    }
}
