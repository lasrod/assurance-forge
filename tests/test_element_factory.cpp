#include "core/assurance_tree.h"
#include "core/acp/assurance_claim_point.h"
#include "core/element_factory.h"
#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <algorithm>
#include <gtest/gtest.h>

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
    for (const auto& e : ac.elements)
        if (e.id == id)
            return true;
    return false;
}

bool SacmHasClaim(const sacm::AssuranceCasePackage& pkg, const std::string& id) {
    for (const auto& ap : pkg.argumentPackages)
        for (const auto& c : ap.claims)
            if (c.id == id)
                return true;
    return false;
}

const parser::SacmElement* FindParserElement(const parser::AssuranceCase& ac, const std::string& id) {
    for (const auto& element : ac.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

bool SacmHasArtifactReference(const sacm::AssuranceCasePackage& pkg, const std::string& id) {
    for (const auto& ap : pkg.argumentPackages)
        for (const auto& artifact_reference : ap.artifactReferences)
            if (artifact_reference.id == id)
                return true;
    return false;
}

bool SacmHasContextRelation(const sacm::AssuranceCasePackage& pkg,
                            const std::string& source_id,
                            const std::string& target_id) {
    for (const auto& ap : pkg.argumentPackages) {
        for (const auto& context : ap.assertedContexts) {
            const bool has_source =
                std::find(context.sources.begin(), context.sources.end(), source_id) != context.sources.end();
            const bool has_target =
                std::find(context.targets.begin(), context.targets.end(), target_id) != context.targets.end();
            if (has_source && has_target)
                return true;
        }
    }
    return false;
}

int CountStrategyInferences(const sacm::AssuranceCasePackage& pkg, const std::string& strategy_id) {
    int count = 0;
    for (const auto& ap : pkg.argumentPackages)
        for (const auto& inference : ap.assertedInferences)
            if (inference.reasoning == strategy_id)
                ++count;
    return count;
}

const sacm::AssertedInference* FindStrategyInference(const sacm::AssuranceCasePackage& pkg,
                                                     const std::string& strategy_id) {
    for (const auto& ap : pkg.argumentPackages)
        for (const auto& inference : ap.assertedInferences)
            if (inference.reasoning == strategy_id)
                return &inference;
    return nullptr;
}

std::string StrategyTargetTag(const sacm::AssuranceCasePackage& pkg, const std::string& strategy_id) {
    for (const auto& ap : pkg.argumentPackages)
        for (const auto& reasoning : ap.argumentReasonings)
            if (reasoning.id == strategy_id)
                for (const auto& tag : reasoning.taggedValues)
                    if (tag.key == "assuranceForge.gsn.strategyTarget")
                        return tag.value;
    return {};
}

const parser::SacmElement* FindModelStrategyInference(const parser::AssuranceCase& ac, const std::string& strategy_id) {
    for (const auto& element : ac.elements)
        if (element.type == "assertedinference" && element.reasoning_ref == strategy_id)
            return &element;
    return nullptr;
}

} // namespace

TEST(ElementFactoryAdd, AddTopGoalCreatesClaimInParserAndSacm) {
    parser::AssuranceCase ac;
    sacm::AssuranceCasePackage pkg;

    std::string new_id;
    std::string err;
    ASSERT_TRUE(core::AddTopGoal(ac, &pkg, new_id, err)) << err;
    ASSERT_FALSE(new_id.empty());

    EXPECT_TRUE(ParserHasId(ac, new_id));
    EXPECT_TRUE(SacmHasClaim(pkg, new_id));
}

TEST(ElementFactoryAdd, AddContextCreatesArtifactReferenceAndAssertedContext) {
    auto mc = MakeRootGoalCase();

    std::string context_id;
    std::string err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Context, context_id, err)) << err;

    const parser::SacmElement* context = FindParserElement(mc.ac, context_id);
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->type, "artifactreference");
    EXPECT_FALSE(SacmHasClaim(mc.pkg, context_id));
    EXPECT_TRUE(SacmHasArtifactReference(mc.pkg, context_id));
    EXPECT_TRUE(SacmHasContextRelation(mc.pkg, context_id, "G1"));

    core::AssuranceTree tree = core::AssuranceTree::Build(mc.ac);
    ASSERT_NE(tree.root, nullptr);
    ASSERT_EQ(tree.root->group2_attachments.size(), 1u);
    EXPECT_EQ(tree.root->group2_attachments.front()->id, context_id);
    EXPECT_EQ(tree.root->group2_attachments.front()->role, core::NodeRole::Context);
}

// A GSN strategy uses the standard single-inference encoding: the strategy is a
// bare ArgumentReasoning carrying a strategyTarget tag (no package inference),
// and its sub-goals become the sources of ONE inference -- materialized on the
// first sub-goal and extended on later ones, not a separate inference each. This
// is what lets a library-primary replay reproduce the on-disk model, since the
// library forbids the legacy bare inference. Phase 9 Stage 7.
TEST(ElementFactoryAdd, StrategyUsesSingleInferenceEncoding) {
    auto mc = MakeRootGoalCase();
    std::string err;

    // Add-strategy: the package gets a reasoning + strategyTarget tag and NO
    // inference; the model gets a render-only placeholder inference so the bare
    // strategy still draws under its goal. No relationship is created.
    std::string strategy_id, strategy_rel;
    ASSERT_TRUE(
        core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Strategy, strategy_id, strategy_rel, err))
        << err;
    EXPECT_TRUE(strategy_rel.empty()) << "add-strategy must not create a relationship";
    EXPECT_EQ(CountStrategyInferences(mc.pkg, strategy_id), 0) << "strategy has no package inference yet";
    EXPECT_EQ(StrategyTargetTag(mc.pkg, strategy_id), "G1");
    const parser::SacmElement* placeholder = FindModelStrategyInference(mc.ac, strategy_id);
    ASSERT_NE(placeholder, nullptr) << "model needs a placeholder inference so the bare strategy renders";
    ASSERT_EQ(placeholder->target_refs.size(), 1u);
    EXPECT_EQ(placeholder->target_refs.front(), "G1");
    EXPECT_TRUE(placeholder->source_refs.empty());

    // First sub-goal materializes the single inference {target=G1, reasoning=S,
    // source=sub}; the render placeholder is replaced by the real relationship.
    std::string sub1, rel1;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, strategy_id, core::NewElementKind::Goal, sub1, rel1, err)) << err;
    EXPECT_FALSE(rel1.empty()) << "materializing the strategy inference creates a relationship";
    ASSERT_EQ(CountStrategyInferences(mc.pkg, strategy_id), 1);
    const sacm::AssertedInference* inference = FindStrategyInference(mc.pkg, strategy_id);
    ASSERT_NE(inference, nullptr);
    ASSERT_EQ(inference->targets.size(), 1u);
    EXPECT_EQ(inference->targets.front(), "G1");
    ASSERT_EQ(inference->sources.size(), 1u);
    EXPECT_EQ(inference->sources.front(), sub1);
    const parser::SacmElement* model_inference = FindModelStrategyInference(mc.ac, strategy_id);
    ASSERT_NE(model_inference, nullptr);
    EXPECT_EQ(model_inference->id, rel1) << "the placeholder was replaced by the real inference id";
    ASSERT_EQ(model_inference->source_refs.size(), 1u);
    EXPECT_EQ(model_inference->source_refs.front(), sub1);

    // Second sub-goal EXTENDS the same inference (one inference, two sources) and
    // creates no new relationship.
    std::string sub2, rel2;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, strategy_id, core::NewElementKind::Goal, sub2, rel2, err)) << err;
    EXPECT_TRUE(rel2.empty()) << "extending an existing inference creates no relationship";
    ASSERT_EQ(CountStrategyInferences(mc.pkg, strategy_id), 1) << "still one inference, not one per sub-goal";
    inference = FindStrategyInference(mc.pkg, strategy_id);
    ASSERT_NE(inference, nullptr);
    ASSERT_EQ(inference->sources.size(), 2u);
    EXPECT_EQ(inference->sources[0], sub1);
    EXPECT_EQ(inference->sources[1], sub2);
}

TEST(ElementFactoryAdd, UsesAcpPackagePrefixInsideConfidenceArgumentPackage) {
    parser::AssuranceCase ac;
    parser::SacmElement top_goal;
    top_goal.id = "ACP1_G1";
    top_goal.type = "claim";
    ac.elements.push_back(top_goal);

    sacm::AssuranceCasePackage pkg;
    sacm::ArgumentPackage confidence_package;
    confidence_package.id = "ACP1_AP";
    core::acp::SetConfidenceArgumentPackage(confidence_package, true);
    sacm::Claim top_claim;
    top_claim.id = "ACP1_G1";
    confidence_package.claims.push_back(top_claim);
    pkg.argumentPackages.push_back(confidence_package);

    std::string child_id;
    std::string err;
    ASSERT_TRUE(core::AddChildElement(ac, &pkg, "ACP1_G1", core::NewElementKind::Goal, child_id, err)) << err;
    EXPECT_EQ(child_id, "ACP1_G2");
    EXPECT_TRUE(ParserHasId(ac, "ACP1_G2"));
    EXPECT_TRUE(ParserHasId(ac, "ACP1_R1"));
    ASSERT_EQ(pkg.argumentPackages.size(), 1u);
    ASSERT_EQ(pkg.argumentPackages.front().assertedInferences.size(), 1u);
    EXPECT_EQ(pkg.argumentPackages.front().assertedInferences.front().id, "ACP1_R1");

    std::string context_id;
    ASSERT_TRUE(core::AddChildElement(ac, &pkg, "ACP1_G1", core::NewElementKind::Context, context_id, err)) << err;
    EXPECT_EQ(context_id, "ACP1_C1");
    EXPECT_TRUE(ParserHasId(ac, "ACP1_R2"));
}

TEST(ElementFactoryRemove, RemoveLeafElement) {
    auto mc = MakeRootGoalCase();

    // Add a Solution under G1.
    std::string new_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Solution, new_id, err)) << err;
    ASSERT_FALSE(new_id.empty());
    ASSERT_TRUE(ParserHasId(mc.ac, new_id));

    // Solution is a leaf: 0 descendants.
    EXPECT_EQ(core::CountDescendants(mc.ac, new_id), 0);

    // Remove using NodeOnly (single-element plan).
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, new_id, core::RemoveMode::NodeOnly, err)) << err;

    // Solution gone from parser model.
    EXPECT_FALSE(ParserHasId(mc.ac, new_id));

    // The originating AssertedEvidence relationship is also gone (it had the
    // removed element as its only source).
    for (const auto& e : mc.ac.elements) {
        EXPECT_NE(e.type, "assertedevidence") << "Dangling assertedevidence relationship left behind: " << e.id;
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
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Strategy, strategy_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Goal, leaf_id, err)) << err;
    // leaf_id was added under G1 directly, not under the strategy. Add another
    // goal under the strategy so we have a real subtree to remove.
    std::string sub_goal_id;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, strategy_id, core::NewElementKind::Goal, sub_goal_id, err))
        << err;

    // The strategy now has at least one descendant.
    EXPECT_GT(core::CountDescendants(mc.ac, strategy_id), 0);

    // NodeAndDescendants cleanly removes the strategy and its sub-goal.
    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, strategy_id, core::RemoveMode::NodeAndDescendants, err)) << err;

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
    EXPECT_FALSE(core::RemoveElement(mc.ac, &mc.pkg, "DOES_NOT_EXIST", core::RemoveMode::NodeOnly, err));
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
    add_node("AR_1", "argumentreasoning"); // strategy
    add_node("CL_A", "claim");
    add_node("CL_B", "claim");

    parser::SacmElement inference;
    inference.id = "INF_1";
    inference.type = "assertedinference";
    inference.target_refs.push_back("CL_TOP");
    inference.reasoning_ref = "AR_1";
    inference.source_refs = {"CL_A", "CL_B"};
    ac.elements.push_back(inference);

    sacm::ArgumentPackage ap;
    sacm::Claim ct;
    ct.id = "CL_TOP";
    ap.claims.push_back(ct);
    sacm::Claim ca;
    ca.id = "CL_A";
    ap.claims.push_back(ca);
    sacm::Claim cb;
    cb.id = "CL_B";
    ap.claims.push_back(cb);
    sacm::ArgumentReasoning ar;
    ar.id = "AR_1";
    ap.argumentReasonings.push_back(ar);
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
    ASSERT_TRUE(core::RemoveElement(ac, &pkg, "CL_A", core::RemoveMode::NodeOnly, err)) << err;

    // CL_A is gone, but the strategy, sibling, target and inference remain.
    EXPECT_FALSE(ParserHasId(ac, "CL_A"));
    EXPECT_TRUE(ParserHasId(ac, "AR_1"));
    EXPECT_TRUE(ParserHasId(ac, "CL_B"));
    EXPECT_TRUE(ParserHasId(ac, "CL_TOP"));
    EXPECT_TRUE(ParserHasId(ac, "INF_1"));

    // Inference's source list no longer contains CL_A.
    for (const auto& e : ac.elements) {
        if (e.id != "INF_1")
            continue;
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
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Strategy, strategy_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, strategy_id, core::NewElementKind::Goal, sub_goal_id, err))
        << err;

    EXPECT_EQ(core::CountDescendants(mc.ac, sub_goal_id), 0);

    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, sub_goal_id, core::RemoveMode::NodeOnly, err)) << err;

    EXPECT_FALSE(ParserHasId(mc.ac, sub_goal_id));
    EXPECT_TRUE(ParserHasId(mc.ac, strategy_id)) << "Strategy must survive removal of its only sub-goal.";
    EXPECT_TRUE(ParserHasId(mc.ac, "G1"));

    // The inference wiring strategy under G1 must still exist (target=G1,
    // reasoning=strategy_id) so the tree continues to render the strategy.
    bool found_wiring = false;
    for (const auto& e : mc.ac.elements) {
        if (e.type != "assertedinference")
            continue;
        bool targets_g1 =
            std::find(e.target_refs.begin(), e.target_refs.end(), std::string("G1")) != e.target_refs.end();
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
    EXPECT_TRUE(strategy_in_tree) << "Tree builder dropped the strategy after sub-goal removal.";
}

// NodeOnly remove of a strategy with structural children must reparent the
// children to the strategy's parent (clearing reasoning of the inference).
TEST(ElementFactoryRemove, RemoveNodeOnly_ReparentsStructuralChildrenToParent) {
    auto mc = MakeRootGoalCase();

    std::string strategy_id, sub_goal_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Strategy, strategy_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, strategy_id, core::NewElementKind::Goal, sub_goal_id, err))
        << err;

    // Plan: NodeOnly on a strategy includes only the strategy itself.
    auto plan = core::PlanRemoval(mc.ac, strategy_id, core::RemoveMode::NodeOnly);
    EXPECT_EQ(plan.size(), 1u);
    EXPECT_TRUE(plan.count(strategy_id));

    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, strategy_id, core::RemoveMode::NodeOnly, err)) << err;

    EXPECT_FALSE(ParserHasId(mc.ac, strategy_id));
    EXPECT_TRUE(ParserHasId(mc.ac, sub_goal_id)) << "Sub-goal must survive a NodeOnly remove of its parent strategy.";

    auto tree = core::AssuranceTree::Build(mc.ac);
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->id, "G1");
    bool sub_goal_under_g1 = false;
    for (const auto* c : tree.root->group1_children) {
        if (c->id == sub_goal_id)
            sub_goal_under_g1 = true;
    }
    EXPECT_TRUE(sub_goal_under_g1) << "Sub-goal must be reparented to G1 after the strategy is removed.";
}

// NodeOnly remove of a regular sub-claim that has its own children must promote
// those children to the grandparent (rewriting the inference target).
TEST(ElementFactoryRemove, RemoveNodeOnly_PromotesGrandchildrenToGrandparent) {
    auto mc = MakeRootGoalCase();

    std::string g2_id, g3_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Goal, g2_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, g2_id, core::NewElementKind::Goal, g3_id, err)) << err;

    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, g2_id, core::RemoveMode::NodeOnly, err)) << err;

    EXPECT_FALSE(ParserHasId(mc.ac, g2_id));
    EXPECT_TRUE(ParserHasId(mc.ac, g3_id));

    auto tree = core::AssuranceTree::Build(mc.ac);
    ASSERT_NE(tree.root, nullptr);
    bool g3_under_g1 = false;
    for (const auto* c : tree.root->group1_children) {
        if (c->id == g3_id)
            g3_under_g1 = true;
    }
    EXPECT_TRUE(g3_under_g1) << "G3 must be reparented to G1 after G2 is removed.";
}

// NodeOnly must also pull in Group2 attachments (Context/Assumption/Justification)
// of the removed node — they cannot survive without their target.
TEST(ElementFactoryRemove, RemoveNodeOnly_AlsoRemovesGroup2Attachments) {
    auto mc = MakeRootGoalCase();

    std::string sub_goal_id, ctx_id, err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Goal, sub_goal_id, err)) << err;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, sub_goal_id, core::NewElementKind::Context, ctx_id, err)) << err;

    auto plan = core::PlanRemoval(mc.ac, sub_goal_id, core::RemoveMode::NodeOnly);
    EXPECT_EQ(plan.size(), 2u);
    EXPECT_TRUE(plan.count(sub_goal_id));
    EXPECT_TRUE(plan.count(ctx_id));

    err.clear();
    ASSERT_TRUE(core::RemoveElement(mc.ac, &mc.pkg, sub_goal_id, core::RemoveMode::NodeOnly, err)) << err;
    EXPECT_FALSE(ParserHasId(mc.ac, sub_goal_id));
    EXPECT_FALSE(ParserHasId(mc.ac, ctx_id)) << "Context attachment must go away with its target node.";
}

// Comprehensive regression for the canonical shared-inference shape (matches
// tests/data/fixture_roundtrip_core_argument.sacm.xml: cl_top / ar_1 / [cl_sub_1, cl_sub_2]
// wired by a single inference inf_1). Exercises both remove modes from every
// relevant vantage point so a sub-claim's removal never disturbs the strategy
// or its other children unexpectedly.
TEST(ElementFactoryRemove, StrategyShape_AllRemoveModes) {
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
    add_node("AR_1", "argumentreasoning");
    add_node("CL_SUB_1", "claim");
    add_node("CL_SUB_2", "claim");

    parser::SacmElement inf;
    inf.id = "INF_1";
    inf.type = "assertedinference";
    inf.target_refs = {"CL_TOP"};
    inf.reasoning_ref = "AR_1";
    inf.source_refs = {"CL_SUB_1", "CL_SUB_2"};
    ac.elements.push_back(inf);

    // Mirror into the SACM package so RemoveElement can scrub both models.
    pkg.argumentPackages.emplace_back();
    auto& ap = pkg.argumentPackages.back();
    ap.id = "AP1";
    sacm::Claim c1;
    c1.id = "CL_TOP";
    ap.claims.push_back(c1);
    sacm::Claim c2;
    c2.id = "CL_SUB_1";
    ap.claims.push_back(c2);
    sacm::Claim c3;
    c3.id = "CL_SUB_2";
    ap.claims.push_back(c3);
    sacm::ArgumentReasoning ar;
    ar.id = "AR_1";
    ap.argumentReasonings.push_back(ar);
    sacm::AssertedInference si;
    si.id = "INF_1";
    si.targets = {"CL_TOP"};
    si.reasoning = "AR_1";
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
        auto ac2 = ac;
        auto pkg2 = pkg;
        std::string err;
        ASSERT_TRUE(core::RemoveElement(ac2, &pkg2, "CL_SUB_1", core::RemoveMode::NodeOnly, err)) << err;
        EXPECT_FALSE(ParserHasId(ac2, "CL_SUB_1"));
        EXPECT_TRUE(ParserHasId(ac2, "CL_SUB_2"));
        EXPECT_TRUE(ParserHasId(ac2, "AR_1"));
        EXPECT_TRUE(ParserHasId(ac2, "CL_TOP"));
    }
    // --- 4. NodeAndDescendants on the strategy: strategy + sub-claims gone, top stays.
    {
        auto ac2 = ac;
        auto pkg2 = pkg;
        std::string err;
        ASSERT_TRUE(core::RemoveElement(ac2, &pkg2, "AR_1", core::RemoveMode::NodeAndDescendants, err)) << err;
        EXPECT_FALSE(ParserHasId(ac2, "AR_1"));
        EXPECT_FALSE(ParserHasId(ac2, "CL_SUB_1"));
        EXPECT_FALSE(ParserHasId(ac2, "CL_SUB_2"));
        EXPECT_TRUE(ParserHasId(ac2, "CL_TOP"));
    }
}

TEST(ElementFactoryRemove, RemovalPlannerUsesFirstExistingTargetRef) {
    parser::AssuranceCase ac;
    parser::SacmElement top;
    top.id = "CL_TOP";
    top.type = "claim";
    ac.elements.push_back(top);

    parser::SacmElement strategy;
    strategy.id = "AR_1";
    strategy.type = "argumentreasoning";
    ac.elements.push_back(strategy);

    parser::SacmElement sub;
    sub.id = "CL_SUB";
    sub.type = "claim";
    ac.elements.push_back(sub);

    parser::SacmElement inf;
    inf.id = "INF_1";
    inf.type = "assertedinference";
    inf.target_refs = {"MISSING_TARGET", "CL_TOP"};
    inf.reasoning_ref = "AR_1";
    inf.source_refs = {"CL_SUB"};
    ac.elements.push_back(inf);

    EXPECT_EQ(core::CountDescendants(ac, "CL_TOP"), 2);

    auto plan = core::PlanRemoval(ac, "CL_TOP", core::RemoveMode::NodeAndDescendants);
    EXPECT_EQ(plan.size(), 3u);
    EXPECT_TRUE(plan.count("CL_TOP"));
    EXPECT_TRUE(plan.count("AR_1"));
    EXPECT_TRUE(plan.count("CL_SUB"));
}

TEST(ElementFactoryRemove, RemovingVisibleTerminologyContextKeepsGlossaryTerm) {
    parser::AssuranceCase ac;
    parser::SacmElement goal;
    goal.id = "G1";
    goal.type = "claim";
    ac.elements.push_back(goal);
    parser::SacmElement term_context;
    term_context.id = "TERM_VISIBLE";
    term_context.name = "ODD";
    term_context.type = "artifactreference";
    ac.elements.push_back(term_context);
    parser::SacmElement relation;
    relation.id = "AC_VISIBLE";
    relation.type = "assertedcontext";
    relation.description = core::kVisibleTerminologyContextMarker;
    relation.source_refs = {"TERM_VISIBLE"};
    relation.target_refs = {"G1"};
    ac.elements.push_back(relation);

    sacm::AssuranceCasePackage package;
    sacm::TerminologyPackage terminology_package;
    terminology_package.id = "TP1";
    sacm::Term term;
    term.id = "TERM_ODD";
    term.value = "ODD";
    terminology_package.terms.push_back(term);
    package.terminologyPackages.push_back(terminology_package);
    sacm::ArgumentPackage argument_package;
    sacm::Claim claim;
    claim.id = "G1";
    argument_package.claims.push_back(claim);
    sacm::ArtifactReference artifact_reference;
    artifact_reference.id = "TERM_VISIBLE";
    artifact_reference.name = "ODD";
    artifact_reference.referencedArtifact = "TERM_ODD";
    argument_package.artifactReferences.push_back(artifact_reference);
    sacm::AssertedContext context;
    context.id = "AC_VISIBLE";
    context.description = core::kVisibleTerminologyContextMarker;
    context.sources = {"TERM_VISIBLE"};
    context.targets = {"G1"};
    argument_package.assertedContexts.push_back(context);
    package.argumentPackages.push_back(argument_package);

    std::string error;
    ASSERT_TRUE(core::RemoveElement(ac, &package, "TERM_VISIBLE", core::RemoveMode::NodeOnly, error)) << error;

    EXPECT_FALSE(ParserHasId(ac, "TERM_VISIBLE"));
    EXPECT_FALSE(ParserHasId(ac, "AC_VISIBLE"));
    ASSERT_EQ(package.terminologyPackages.size(), 1u);
    ASSERT_EQ(package.terminologyPackages.front().terms.size(), 1u);
    EXPECT_EQ(package.terminologyPackages.front().terms.front().id, "TERM_ODD");
    ASSERT_EQ(package.argumentPackages.size(), 1u);
    EXPECT_TRUE(package.argumentPackages.front().artifactReferences.empty());
    EXPECT_TRUE(package.argumentPackages.front().assertedContexts.empty());
}

TEST(ElementFactoryRemove, RemovalPlannerIgnoresDanglingReasoningAndSourceRefs) {
    parser::AssuranceCase ac;
    parser::SacmElement top;
    top.id = "CL_TOP";
    top.type = "claim";
    ac.elements.push_back(top);

    parser::SacmElement sub;
    sub.id = "CL_SUB";
    sub.type = "claim";
    ac.elements.push_back(sub);

    parser::SacmElement inf;
    inf.id = "INF_1";
    inf.type = "assertedinference";
    inf.target_refs = {"CL_TOP"};
    inf.reasoning_ref = "MISSING_REASONING";
    inf.source_refs = {"MISSING_SOURCE", "CL_SUB"};
    ac.elements.push_back(inf);

    EXPECT_EQ(core::CountDescendants(ac, "CL_TOP"), 1);

    auto plan = core::PlanRemoval(ac, "CL_TOP", core::RemoveMode::NodeAndDescendants);
    EXPECT_EQ(plan.size(), 2u);
    EXPECT_TRUE(plan.count("CL_TOP"));
    EXPECT_TRUE(plan.count("CL_SUB"));
    EXPECT_FALSE(plan.count("MISSING_REASONING"));
    EXPECT_FALSE(plan.count("MISSING_SOURCE"));
}

// ===== AF-GSN-012: the GSN element identifier contract =====
//
// GSN v3 makes the element identifier mandatory, and the diagram shows it on
// every node. Storage identity remains the stable key used by SACM references;
// the notation identifier is independently editable and is what users see.

TEST(ElementIdentifierTest, NewElementsGetGsnConventionalPrefixes) {
    MiniCase mc = MakeRootGoalCase();
    std::string err;

    std::string goal_id;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Goal, goal_id, err)) << err;
    std::string solution_id;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Solution, solution_id, err)) << err;

    EXPECT_EQ(goal_id.rfind("G", 0), 0u) << "goal identifier was '" << goal_id << "'";
    EXPECT_EQ(solution_id.rfind("Sn", 0), 0u) << "solution identifier was '" << solution_id << "'";
}

TEST(ElementIdentifierTest, IdentifiersAreUniqueWithinTheCase) {
    MiniCase mc = MakeRootGoalCase();
    std::string err;

    std::vector<std::string> minted;
    for (int i = 0; i < 5; ++i) {
        std::string id;
        ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Goal, id, err)) << err;
        minted.push_back(id);
    }

    std::sort(minted.begin(), minted.end());
    EXPECT_EQ(std::adjacent_find(minted.begin(), minted.end()), minted.end())
        << "two elements were minted the same identifier";
    // The seeded G1 must not be reused either.
    EXPECT_EQ(std::find(minted.begin(), minted.end(), "G1"), minted.end());
}

TEST(ElementIdentifierTest, PlannedIdMatchesTheIdActuallyMinted) {
    // Audit payloads record the planned id, so a divergence here would write an
    // event naming an element that does not exist.
    MiniCase mc = MakeRootGoalCase();
    std::string err;
    std::string planned_element;
    std::string planned_relationship;
    ASSERT_TRUE(core::PlanChildElementIds(
        mc.ac, &mc.pkg, "G1", core::NewElementKind::Goal, planned_element, planned_relationship, err))
        << err;

    std::string actual;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Goal, actual, err)) << err;
    EXPECT_EQ(actual, planned_element);
}

TEST(ElementIdentifierTest, IdentifierIsRenderedInTheNodeLabel) {
    MiniCase mc = MakeRootGoalCase();
    std::string err;
    std::string goal_id;
    ASSERT_TRUE(core::AddChildElement(mc.ac, &mc.pkg, "G1", core::NewElementKind::Goal, goal_id, err)) << err;

    const core::AssuranceTree tree = core::AssuranceTree::Build(mc.ac, "ja");
    const core::TreeNode* node = nullptr;
    for (const auto& owned : tree.nodes) {
        if (owned->id == goal_id)
            node = owned.get();
    }
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->label.rfind(goal_id, 0), 0u)
        << "identifier missing from the rendered label: '" << node->label << "'";

    // A freshly added element has no name yet, so the label must be the bare
    // identifier: the ": " separator belongs to the name, and emitting it here
    // renders on the canvas as an identifier trailing a colon into nothing.
    EXPECT_FALSE(node->has_name);
    const std::string first_line = node->label.substr(0, node->label.find('\n'));
    EXPECT_EQ(first_line, goal_id) << "expected a bare identifier, got: '" << first_line << "'";
}

TEST(ElementIdentifierTest, GSN3_CORE_010_NotationIdentifierCanChangeWithoutChangingStorageIdentity) {
    MiniCase mc = MakeRootGoalCase();
    mc.ac.elements.front().gsn_identifier = "G1";

    std::string old_identifier;
    std::string error;
    ASSERT_TRUE(core::SetGsnIdentifier(mc.ac, &mc.pkg, "G1", "SYS-GOAL", old_identifier, error)) << error;

    EXPECT_EQ(old_identifier, "G1");
    EXPECT_EQ(mc.ac.elements.front().id, "G1");
    EXPECT_EQ(mc.ac.elements.front().gsn_identifier, "SYS-GOAL");
    ASSERT_EQ(mc.pkg.argumentPackages.front().claims.size(), 1u);
    const sacm::Claim& claim = mc.pkg.argumentPackages.front().claims.front();
    EXPECT_EQ(claim.id, "G1");
    ASSERT_EQ(claim.taggedValues.size(), 1u);
    EXPECT_EQ(claim.taggedValues.front().key, core::kGsnIdentifierTagKey);
    EXPECT_EQ(claim.taggedValues.front().value, "SYS-GOAL");

    core::AssuranceTree tree = core::AssuranceTree::Build(mc.ac);
    ASSERT_NE(tree.root, nullptr);
    EXPECT_EQ(tree.root->id, "G1");
    EXPECT_EQ(tree.root->label, "SYS-GOAL: Top Goal");
}

TEST(ElementIdentifierTest, GSN3_CORE_010_NotationIdentifiersMustBeNonEmptyAndUnique) {
    MiniCase mc = MakeRootGoalCase();
    mc.ac.elements.front().gsn_identifier = "G1";
    parser::SacmElement second;
    second.id = "storage-2";
    second.gsn_identifier = "G2";
    second.type = "claim";
    mc.ac.elements.push_back(second);

    std::string old_identifier;
    std::string error;
    EXPECT_FALSE(core::SetGsnIdentifier(mc.ac, &mc.pkg, "G1", " ", old_identifier, error));
    EXPECT_NE(error.find("non-empty"), std::string::npos);

    error.clear();
    EXPECT_FALSE(core::SetGsnIdentifier(mc.ac, &mc.pkg, "G1", "G2", old_identifier, error));
    EXPECT_NE(error.find("already used"), std::string::npos);
    EXPECT_EQ(mc.ac.elements.front().gsn_identifier, "G1");
}
