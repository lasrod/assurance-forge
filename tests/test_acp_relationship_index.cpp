#include "core/acp/acp_relationship_index.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

parser::SacmElement Element(std::string id, std::string type, std::string assertion_declaration = {}) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    element.assertion_declaration = std::move(assertion_declaration);
    return element;
}

parser::SacmElement Relationship(std::string id,
                                 std::string type,
                                 std::vector<std::string> targets,
                                 std::vector<std::string> sources,
                                 std::string reasoning = {}) {
    parser::SacmElement relationship;
    relationship.id = std::move(id);
    relationship.type = std::move(type);
    relationship.target_refs = std::move(targets);
    relationship.source_refs = std::move(sources);
    relationship.reasoning_ref = std::move(reasoning);
    return relationship;
}

parser::AssuranceCase MakeStrategyCase() {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim"));
    model.elements.push_back(Element("S1", "argumentreasoning"));
    model.elements.push_back(Element("G2", "claim"));
    model.elements.push_back(Relationship("R1", "assertedinference", {"G1"}, {"G2"}, "S1"));
    return model;
}

} // namespace

TEST(AcpRelationshipIndexTest, AllowsAcpOnGoalToStrategyAndBlocksStrategyToGoal) {
    const std::vector<core::acp::AcpRelationshipTarget> targets =
        core::acp::BuildAcpRelationshipTargets(MakeStrategyCase());

    const core::acp::AcpRelationshipTarget* goal_to_strategy =
        core::acp::FindAcpRelationshipTarget(targets, "G1", "S1");
    ASSERT_NE(goal_to_strategy, nullptr);
    EXPECT_EQ(goal_to_strategy->relationship_id, "R1");
    EXPECT_TRUE(goal_to_strategy->eligible_for_acp);
    EXPECT_FALSE(goal_to_strategy->strategy_child_edge);

    const core::acp::AcpRelationshipTarget* strategy_to_goal =
        core::acp::FindAcpRelationshipTarget(targets, "S1", "G2");
    ASSERT_NE(strategy_to_goal, nullptr);
    EXPECT_EQ(strategy_to_goal->relationship_id, "R1");
    EXPECT_FALSE(strategy_to_goal->eligible_for_acp);
    EXPECT_TRUE(strategy_to_goal->strategy_child_edge);
    EXPECT_NE(strategy_to_goal->blocked_reason.find("Strategy -> Goal"), std::string::npos);
}

TEST(AcpRelationshipIndexTest, MapsDirectSupportedByEvidenceAndContextRelationships) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim"));
    model.elements.push_back(Element("G2", "claim"));
    model.elements.push_back(Element("Sn1", "artifactreference"));
    model.elements.push_back(Element("C1", "artifactreference"));
    model.elements.push_back(Relationship("R1", "assertedinference", {"G1"}, {"G2"}));
    model.elements.push_back(Relationship("R2", "assertedevidence", {"G2"}, {"Sn1"}));
    model.elements.push_back(Relationship("R3", "assertedcontext", {"G1"}, {"C1"}));

    const std::vector<core::acp::AcpRelationshipTarget> targets = core::acp::BuildAcpRelationshipTargets(model);

    const auto* support = core::acp::FindAcpRelationshipTarget(targets, "G1", "G2");
    ASSERT_NE(support, nullptr);
    EXPECT_EQ(support->relationship_id, "R1");
    EXPECT_EQ(support->kind, core::acp::AcpRelationshipKind::SupportedBy);
    EXPECT_TRUE(support->eligible_for_acp);

    const auto* evidence = core::acp::FindAcpRelationshipTarget(targets, "G2", "Sn1");
    ASSERT_NE(evidence, nullptr);
    EXPECT_EQ(evidence->relationship_id, "R2");
    EXPECT_EQ(evidence->kind, core::acp::AcpRelationshipKind::AssertedEvidence);
    EXPECT_TRUE(evidence->eligible_for_acp);

    const auto* context = core::acp::FindAcpRelationshipTarget(targets, "G1", "C1");
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->relationship_id, "R3");
    EXPECT_EQ(context->kind, core::acp::AcpRelationshipKind::InContextOf);
    EXPECT_TRUE(context->eligible_for_acp);
}
