#include "core/assurance_tree.h"
#include "core/tree_editing.h"
#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace {

struct MiniCase {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
};

void AddClaim(MiniCase& mini_case, const std::string& id, const std::string& assertion_declaration = {}) {
    parser::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    element.assertion_declaration = assertion_declaration;
    mini_case.model.elements.push_back(element);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::Claim claim;
    claim.id = id;
    claim.name = id;
    claim.assertionDeclaration = assertion_declaration;
    mini_case.package.argumentPackages.front().claims.push_back(claim);
}

void AddStrategy(MiniCase& mini_case, const std::string& id) {
    parser::SacmElement element;
    element.id = id;
    element.type = "argumentreasoning";
    element.name = id;
    mini_case.model.elements.push_back(element);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::ArgumentReasoning reasoning;
    reasoning.id = id;
    reasoning.name = id;
    mini_case.package.argumentPackages.front().argumentReasonings.push_back(reasoning);
}

void AddArtifactReference(MiniCase& mini_case, const std::string& id) {
    parser::SacmElement element;
    element.id = id;
    element.type = "artifactreference";
    element.name = id;
    mini_case.model.elements.push_back(element);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::ArtifactReference artifact_reference;
    artifact_reference.id = id;
    artifact_reference.name = id;
    mini_case.package.argumentPackages.front().artifactReferences.push_back(artifact_reference);
}

void AddInference(MiniCase& mini_case,
                  const std::string& id,
                  const std::string& target,
                  std::vector<std::string> sources,
                  const std::string& reasoning = {}) {
    parser::SacmElement relationship;
    relationship.id = id;
    relationship.type = "assertedinference";
    relationship.target_refs.push_back(target);
    relationship.source_refs = sources;
    relationship.reasoning_ref = reasoning;
    mini_case.model.elements.push_back(relationship);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::AssertedInference inference;
    inference.id = id;
    inference.targets.push_back(target);
    inference.sources = sources;
    inference.reasoning = reasoning;
    mini_case.package.argumentPackages.front().assertedInferences.push_back(inference);
}

void AddContext(MiniCase& mini_case, const std::string& id, const std::string& target, const std::string& source) {
    parser::SacmElement relationship;
    relationship.id = id;
    relationship.type = "assertedcontext";
    relationship.target_refs.push_back(target);
    relationship.source_refs.push_back(source);
    mini_case.model.elements.push_back(relationship);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::AssertedContext context;
    context.id = id;
    context.targets.push_back(target);
    context.sources.push_back(source);
    mini_case.package.argumentPackages.front().assertedContexts.push_back(context);
}

void AddEvidence(MiniCase& mini_case, const std::string& id, const std::string& target, const std::string& source) {
    parser::SacmElement relationship;
    relationship.id = id;
    relationship.type = "assertedevidence";
    relationship.target_refs.push_back(target);
    relationship.source_refs.push_back(source);
    mini_case.model.elements.push_back(relationship);

    if (mini_case.package.argumentPackages.empty())
        mini_case.package.argumentPackages.emplace_back();
    sacm::AssertedEvidence evidence;
    evidence.id = id;
    evidence.targets.push_back(target);
    evidence.sources.push_back(source);
    mini_case.package.argumentPackages.front().assertedEvidences.push_back(evidence);
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

bool HasRelationship(const parser::AssuranceCase& model,
                     const std::string& type,
                     const std::string& target,
                     const std::string& source) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.type != type)
            continue;
        if (std::find(element.target_refs.begin(), element.target_refs.end(), target) == element.target_refs.end())
            continue;
        if (std::find(element.source_refs.begin(), element.source_refs.end(), source) != element.source_refs.end())
            return true;
    }
    return false;
}

bool HasStrategyRelationship(const parser::AssuranceCase& model,
                             const std::string& target,
                             const std::string& strategy) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.type == "assertedinference" && element.reasoning_ref == strategy &&
            std::find(element.target_refs.begin(), element.target_refs.end(), target) != element.target_refs.end()) {
            return true;
        }
    }
    return false;
}

MiniCase MakeSiblingCase() {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "G2");
    AddClaim(mini_case, "G3");
    AddClaim(mini_case, "G4");
    AddInference(mini_case, "R1", "G1", {"G2", "G3", "G4"});
    return mini_case;
}

MiniCase MakeMoveCase() {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "G2");
    AddClaim(mini_case, "G3");
    AddClaim(mini_case, "G4");
    AddInference(mini_case, "R1", "G1", {"G2", "G3"});
    AddInference(mini_case, "R2", "G2", {"G4"});
    return mini_case;
}

} // namespace

TEST(TreeEditingValidation, AllowsSameParentSiblingReorder) {
    MiniCase mini_case = MakeSiblingCase();
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    const core::TreeEditIndex index = core::BuildTreeEditIndex(mini_case.model);

    core::TreeDropValidationResult result = core::ValidateTreeDrop(index, tree, "G4", "G2", core::TreeDropMode::Before);

    EXPECT_TRUE(result.allowed) << result.reason;
    EXPECT_FALSE(result.changes_semantic_relationship);
}

TEST(TreeEditingValidation, ReusesBuiltIndexAcrossDropValidations) {
    MiniCase mini_case = MakeMoveCase();
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    const core::TreeEditIndex index = core::BuildTreeEditIndex(mini_case.model);

    core::TreeDropValidationResult reorder_result =
        core::ValidateTreeDrop(index, tree, "G4", "G3", core::TreeDropMode::Before);
    core::TreeDropValidationResult child_result =
        core::ValidateTreeDrop(index, tree, "G4", "G3", core::TreeDropMode::AsChild);

    EXPECT_FALSE(reorder_result.allowed);
    EXPECT_TRUE(child_result.allowed) << child_result.reason;
    EXPECT_TRUE(child_result.changes_semantic_relationship);
}

TEST(TreeEditingValidation, RejectsCrossParentBeforeDrop) {
    MiniCase mini_case = MakeMoveCase();
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);

    core::TreeDropValidationResult result =
        core::ValidateTreeDrop(mini_case.model, tree, "G4", "G3", core::TreeDropMode::Before);

    EXPECT_FALSE(result.allowed);
}

TEST(TreeEditingValidation, RejectsParentIntoDescendant) {
    MiniCase mini_case = MakeMoveCase();
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);

    core::TreeDropValidationResult result =
        core::ValidateTreeDrop(mini_case.model, tree, "G2", "G4", core::TreeDropMode::AsChild);

    EXPECT_FALSE(result.allowed);
}

TEST(TreeEditingValidation, RejectsGoalUnderSolution) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "G2");
    AddArtifactReference(mini_case, "Sn1");
    AddInference(mini_case, "R1", "G1", {"G2"});
    AddEvidence(mini_case, "R2", "G1", "Sn1");
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);

    core::TreeDropValidationResult result =
        core::ValidateTreeDrop(mini_case.model, tree, "G2", "Sn1", core::TreeDropMode::AsChild);

    EXPECT_FALSE(result.allowed);
}

TEST(TreeEditingValidation, AllowsContextUnderStrategy) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "C1");
    AddStrategy(mini_case, "S1");
    AddInference(mini_case, "R1", "G1", {}, "S1");
    AddContext(mini_case, "R2", "G1", "C1");
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);

    core::TreeDropValidationResult result =
        core::ValidateTreeDrop(mini_case.model, tree, "C1", "S1", core::TreeDropMode::AsChild);

    EXPECT_TRUE(result.allowed) << result.reason;
    EXPECT_EQ(result.relationship_kind, core::TreeRelationshipKind::InContextOf);
}

// GSN v3: SupportedBy runs from a Goal *or a Strategy* to a Goal, Strategy or
// Solution, so a strategy discharged directly by evidence is ordinary GSN. The
// drop used to refuse it while the Add menu created exactly the same thing --
// the tool disagreeing with itself about its own notation (GSN3-CORE-015).
//
// The expected kind is `AssertedEvidence` rather than the value spelled
// `SupportedBy` because SACM splits GSN's one relationship in two by what sits
// at the far end: a Solution there is written as `assertedevidence`. See the
// vocabulary note on TreeRelationshipKind.
TEST(TreeEditingValidation, AllowsSolutionUnderStrategy) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddStrategy(mini_case, "S1");
    AddArtifactReference(mini_case, "Sn1");
    AddInference(mini_case, "R1", "G1", {}, "S1");
    AddEvidence(mini_case, "R2", "G1", "Sn1");
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);

    core::TreeDropValidationResult result =
        core::ValidateTreeDrop(mini_case.model, tree, "Sn1", "S1", core::TreeDropMode::AsChild);

    EXPECT_TRUE(result.allowed) << result.reason;
    EXPECT_EQ(result.relationship_kind, core::TreeRelationshipKind::AssertedEvidence);
}

// Still refused, and deliberately: GSN permits it and SACM would hold it, but
// the single-inference encoding records the goal a strategy supports in a
// `strategyTarget` tag and nothing has established what that means when the
// target is itself a strategy. A limit of the encoding, not of the notation.
TEST(TreeEditingValidation, RejectsStrategyUnderStrategy) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddStrategy(mini_case, "S1");
    AddStrategy(mini_case, "S2");
    AddInference(mini_case, "R1", "G1", {}, "S1");
    AddInference(mini_case, "R2", "G1", {}, "S2");
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);

    core::TreeDropValidationResult result =
        core::ValidateTreeDrop(mini_case.model, tree, "S2", "S1", core::TreeDropMode::AsChild);

    EXPECT_FALSE(result.allowed);
}

TEST(TreeEditingCommand, ReorderSiblingsPersistsSourceOrderAndDisplayOrder) {
    MiniCase mini_case = MakeSiblingCase();
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    core::TreeDisplayOrder order;
    std::string error;

    ASSERT_TRUE(core::ReorderSiblings(mini_case.model,
                                      &mini_case.package,
                                      tree,
                                      order,
                                      core::ReorderSiblingsCommand{"G4", "G2", core::TreeDropMode::Before},
                                      error))
        << error;

    ASSERT_EQ(order.children_by_parent.count("G1"), 1u);
    EXPECT_EQ(order.children_by_parent["G1"], std::vector<std::string>({"G4", "G2", "G3"}));
    ASSERT_EQ(mini_case.model.elements.size(), 5u);
    const parser::SacmElement* relationship = FindElement(mini_case.model, "R1");
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->source_refs, std::vector<std::string>({"G4", "G2", "G3"}));
    ASSERT_EQ(mini_case.package.argumentPackages.size(), 1u);
    ASSERT_EQ(mini_case.package.argumentPackages.front().assertedInferences.size(), 1u);
    EXPECT_EQ(mini_case.package.argumentPackages.front().assertedInferences.front().sources,
              std::vector<std::string>({"G4", "G2", "G3"}));
}

TEST(TreeEditingCommand, ApplyDisplayOrderReordersBuiltTreeProjection) {
    MiniCase mini_case = MakeSiblingCase();
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    core::TreeDisplayOrder order;
    std::string error;

    ASSERT_TRUE(core::ReorderSiblings(mini_case.model,
                                      &mini_case.package,
                                      tree,
                                      order,
                                      core::ReorderSiblingsCommand{"G4", "G2", core::TreeDropMode::Before},
                                      error))
        << error;

    core::AssuranceTree rebuilt_tree = core::AssuranceTree::Build(mini_case.model);
    core::ApplyTreeDisplayOrder(rebuilt_tree, order);

    ASSERT_NE(rebuilt_tree.root, nullptr);
    ASSERT_EQ(rebuilt_tree.root->group1_children.size(), 3u);
    EXPECT_EQ(rebuilt_tree.root->group1_children[0]->id, "G4");
    EXPECT_EQ(rebuilt_tree.root->group1_children[1]->id, "G2");
    EXPECT_EQ(rebuilt_tree.root->group1_children[2]->id, "G3");
}

TEST(TreeEditingCommand, ReorderSolutionsPersistsByReorderingEvidenceRelationships) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddArtifactReference(mini_case, "Sn1");
    AddArtifactReference(mini_case, "Sn2");
    AddArtifactReference(mini_case, "Sn3");
    AddEvidence(mini_case, "R1", "G1", "Sn1");
    AddEvidence(mini_case, "R2", "G1", "Sn2");
    AddEvidence(mini_case, "R3", "G1", "Sn3");
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    core::TreeDisplayOrder order;
    std::string error;

    ASSERT_TRUE(core::ReorderSiblings(mini_case.model,
                                      &mini_case.package,
                                      tree,
                                      order,
                                      core::ReorderSiblingsCommand{"Sn3", "Sn1", core::TreeDropMode::Before},
                                      error))
        << error;

    core::AssuranceTree rebuilt_tree = core::AssuranceTree::Build(mini_case.model);
    ASSERT_NE(rebuilt_tree.root, nullptr);
    ASSERT_EQ(rebuilt_tree.root->group1_children.size(), 3u);
    EXPECT_EQ(rebuilt_tree.root->group1_children[0]->id, "Sn3");
    EXPECT_EQ(rebuilt_tree.root->group1_children[1]->id, "Sn1");
    EXPECT_EQ(rebuilt_tree.root->group1_children[2]->id, "Sn2");

    ASSERT_EQ(mini_case.package.argumentPackages.size(), 1u);
    const auto& evidences = mini_case.package.argumentPackages.front().assertedEvidences;
    ASSERT_EQ(evidences.size(), 3u);
    EXPECT_EQ(evidences[0].id, "R3");
    EXPECT_EQ(evidences[1].id, "R1");
    EXPECT_EQ(evidences[2].id, "R2");
}

TEST(TreeEditingCommand, MoveGoalUnderGoalRewritesOnlyIncomingRelationship) {
    MiniCase mini_case = MakeMoveCase();
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    std::string error;

    ASSERT_TRUE(
        core::MoveSubtree(mini_case.model, &mini_case.package, tree, core::MoveSubtreeCommand{"G4", "G3"}, error))
        << error;

    const parser::SacmElement* old_relationship = FindElement(mini_case.model, "R2");
    EXPECT_EQ(old_relationship, nullptr);
    EXPECT_TRUE(HasRelationship(mini_case.model, "assertedinference", "G3", "G4"));
    EXPECT_TRUE(HasRelationship(mini_case.model, "assertedinference", "G1", "G2"));

    core::AssuranceTree moved_tree = core::AssuranceTree::Build(mini_case.model);
    const core::TreeNode* g3 = nullptr;
    for (const core::TreeNode* child : moved_tree.root->group1_children) {
        if (child->id == "G3")
            g3 = child;
    }
    ASSERT_NE(g3, nullptr);
    ASSERT_EQ(g3->group1_children.size(), 1u);
    EXPECT_EQ(g3->group1_children.front()->id, "G4");
}

TEST(TreeEditingCommand, MoveStrategyRetargetsItsInferenceAndPreservesSubtree) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "G2");
    AddClaim(mini_case, "G3");
    AddStrategy(mini_case, "S1");
    AddInference(mini_case, "R1", "G1", {"G2"}, "S1");
    AddInference(mini_case, "R2", "G1", {"G3"});
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    std::string error;

    ASSERT_TRUE(
        core::MoveSubtree(mini_case.model, &mini_case.package, tree, core::MoveSubtreeCommand{"S1", "G3"}, error))
        << error;

    EXPECT_TRUE(HasStrategyRelationship(mini_case.model, "G3", "S1"));
    EXPECT_FALSE(HasStrategyRelationship(mini_case.model, "G1", "S1"));
    const parser::SacmElement* relationship = FindElement(mini_case.model, "R1");
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->source_refs, std::vector<std::string>({"G2"}));
}

// `PlanMoveSubtreeFromDiff` is what lets the command write a move through the
// library seams instead of the bridge. It must report the whole change -- and
// report up front the shapes a seam cannot express, because the caller can only
// fall back to the bridge BEFORE it has written anything.
TEST(TreeEditingCommand, MoveSubtreePlanReportsCreatedRetargetedAndDeleted) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "G2");
    AddClaim(mini_case, "G3");
    AddClaim(mini_case, "G4");
    // R1 has two sources, so moving one leaves it valid rather than empty.
    AddInference(mini_case, "R1", "G1", {"G2", "G3"});
    AddInference(mini_case, "R2", "G1", {"G4"});
    const parser::AssuranceCase before = mini_case.model;

    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    std::string error;
    ASSERT_TRUE(
        core::MoveSubtree(mini_case.model, &mini_case.package, tree, core::MoveSubtreeCommand{"G3", "G4"}, error))
        << error;

    const core::MoveSubtreePlan plan = core::PlanMoveSubtreeFromDiff(before, mini_case.model);
    EXPECT_TRUE(plan.unrepresentable_reason.empty()) << plan.unrepresentable_reason;
    EXPECT_FALSE(plan.touches_non_relationships);
    ASSERT_EQ(plan.created.size(), 1u);
    EXPECT_EQ(plan.created.front().type, "assertedinference");
    EXPECT_EQ(plan.created.front().sources, std::vector<std::string>({"G3"}));
    EXPECT_EQ(plan.created.front().targets, std::vector<std::string>({"G4"}));
    ASSERT_EQ(plan.retargeted.size(), 1u);
    EXPECT_EQ(plan.retargeted.front().id, "R1");
    EXPECT_EQ(plan.retargeted.front().sources, std::vector<std::string>({"G2"}))
        << "the plan did not notice that R1 lost a source";
    EXPECT_TRUE(plan.deleted_ids.empty()) << "nothing had to be deleted; R1 still has a source";
}

// The shape that must NOT be written through the seams. An AssertedInference with
// a `reasoning` is not "dangling" by the legacy test even with no sources
// (`IsParserRelationshipDangling`), so moving its only sub-goal leaves it in the
// model with nothing supporting it -- and SACM 11.13 gives source [1..*], so the
// library refuses. Caught in the plan, before a single seam has run, because the
// alternative is a half-moved argument with no way back.
TEST(TreeEditingCommand, MoveSubtreePlanRefusesAMoveThatEmptiesTheOldInference) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "G2");
    AddClaim(mini_case, "G3");
    AddStrategy(mini_case, "S1");
    // R1's only source is G2, and its reasoning keeps it alive when G2 leaves.
    AddInference(mini_case, "R1", "G1", {"G2"}, "S1");
    AddInference(mini_case, "R2", "G1", {"G3"});
    const parser::AssuranceCase before = mini_case.model;

    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    std::string error;
    ASSERT_TRUE(
        core::MoveSubtree(mini_case.model, &mini_case.package, tree, core::MoveSubtreeCommand{"G2", "G3"}, error))
        << error;
    const parser::SacmElement* emptied = FindElement(mini_case.model, "R1");
    ASSERT_NE(emptied, nullptr) << "R1 was deleted, so this test no longer measures the surviving-empty case";
    ASSERT_TRUE(emptied->source_refs.empty()) << "R1 kept a source; the fixture no longer produces the shape";

    const core::MoveSubtreePlan plan = core::PlanMoveSubtreeFromDiff(before, mini_case.model);
    EXPECT_FALSE(plan.unrepresentable_reason.empty())
        << "the plan would have written an inference with no source through the seams";
    EXPECT_NE(plan.unrepresentable_reason.find("R1"), std::string::npos)
        << "the reason does not say which relationship: " << plan.unrepresentable_reason;
}

TEST(TreeEditingCommand, MoveSolutionUnderGoalCreatesEvidenceRelationship) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "G2");
    AddArtifactReference(mini_case, "Sn1");
    AddInference(mini_case, "R1", "G1", {"G2"});
    AddEvidence(mini_case, "R2", "G1", "Sn1");
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    std::string error;

    ASSERT_TRUE(
        core::MoveSubtree(mini_case.model, &mini_case.package, tree, core::MoveSubtreeCommand{"Sn1", "G2"}, error))
        << error;

    EXPECT_TRUE(HasRelationship(mini_case.model, "assertedevidence", "G2", "Sn1"));
}

TEST(TreeEditingCommand, MoveAssumptionUnderStrategyCreatesContextRelationship) {
    MiniCase mini_case;
    AddClaim(mini_case, "G1");
    AddClaim(mini_case, "A1", "assumed");
    AddStrategy(mini_case, "S1");
    AddInference(mini_case, "R1", "G1", {}, "S1");
    AddContext(mini_case, "R2", "G1", "A1");
    core::AssuranceTree tree = core::AssuranceTree::Build(mini_case.model);
    std::string error;

    ASSERT_TRUE(
        core::MoveSubtree(mini_case.model, &mini_case.package, tree, core::MoveSubtreeCommand{"A1", "S1"}, error))
        << error;

    EXPECT_TRUE(HasRelationship(mini_case.model, "assertedcontext", "S1", "A1"));
}
