#include "core/acp/acp_editing.h"

#include "core/acp/assurance_claim_point.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

parser::SacmElement ParserElement(std::string id, std::string type) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    return element;
}

parser::SacmElement ParserRelationship(std::string id) {
    parser::SacmElement relationship;
    relationship.id = std::move(id);
    relationship.type = "assertedinference";
    relationship.target_refs = {"G1"};
    relationship.source_refs = {"G2"};
    return relationship;
}

sacm::Claim Claim(std::string id) {
    sacm::Claim claim;
    claim.id = std::move(id);
    return claim;
}

struct AcpEditingFixture {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;

    AcpEditingFixture() {
        model.elements.push_back(ParserElement("G1", "claim"));
        model.elements.push_back(ParserElement("G2", "claim"));
        model.elements.push_back(ParserRelationship("R1"));

        sacm::ArgumentPackage argument_package;
        argument_package.id = "AP1";
        argument_package.claims.push_back(Claim("G1"));
        argument_package.claims.push_back(Claim("G2"));
        sacm::AssertedInference inference;
        inference.id = "R1";
        inference.targets = {"G1"};
        inference.sources = {"G2"};
        argument_package.assertedInferences.push_back(std::move(inference));
        package.argumentPackages.push_back(std::move(argument_package));
    }
};

} // namespace

TEST(AcpEditingTest, AddsUpdatesAndRemovesRelationshipAcpInParserAndSacm) {
    AcpEditingFixture fixture;

    core::acp::AcpEditResult added = core::acp::AddAcp(fixture.model, &fixture.package, "relationship", "R1");
    ASSERT_TRUE(added.changed) << added.error;
    EXPECT_EQ(added.acp_id, "ACP1");
    ASSERT_EQ(fixture.model.acps.size(), 1u);
    EXPECT_EQ(fixture.model.acps[0].target_id, "R1");
    EXPECT_EQ(fixture.model.acps[0].resolution_kind, "none");
    ASSERT_EQ(core::acp::CollectAcps(fixture.package).size(), 1u);

    parser::AcpRecord edited = fixture.model.acps[0];
    edited.resolution_kind = "text";
    edited.text = "Confidence in this inference is sufficient.";
    core::acp::AcpEditResult updated = core::acp::UpsertAcp(fixture.model, &fixture.package, edited);
    ASSERT_TRUE(updated.changed) << updated.error;
    ASSERT_EQ(fixture.model.acps.size(), 1u);
    EXPECT_EQ(fixture.model.acps[0].text, edited.text);

    const std::vector<core::acp::Acp> sacm_acps = core::acp::CollectAcps(fixture.package);
    ASSERT_EQ(sacm_acps.size(), 1u);
    EXPECT_EQ(sacm_acps[0].resolution.kind, core::acp::AcpResolutionKind::Text);
    EXPECT_EQ(sacm_acps[0].resolution.text, edited.text);

    core::acp::AcpEditResult removed = core::acp::RemoveAcp(fixture.model, &fixture.package, "ACP1");
    ASSERT_TRUE(removed.changed) << removed.error;
    EXPECT_TRUE(fixture.model.acps.empty());
    EXPECT_TRUE(core::acp::CollectAcps(fixture.package).empty());
}

TEST(AcpEditingTest, RejectsMissingSacmTargetWithoutChangingParserProjection) {
    AcpEditingFixture fixture;
    fixture.package.argumentPackages.front().assertedInferences.clear();

    core::acp::AcpEditResult added = core::acp::AddAcp(fixture.model, &fixture.package, "relationship", "R1");
    EXPECT_FALSE(added.changed);
    EXPECT_FALSE(added.error.empty());
    EXPECT_TRUE(fixture.model.acps.empty());
}

TEST(AcpEditingTest, AllowsOnlyOneAcpPerRelationship) {
    AcpEditingFixture fixture;

    core::acp::AcpEditResult first = core::acp::AddAcp(fixture.model, &fixture.package, "relationship", "R1");
    ASSERT_TRUE(first.changed) << first.error;

    core::acp::AcpEditResult second = core::acp::AddAcp(fixture.model, &fixture.package, "relationship", "R1");
    EXPECT_FALSE(second.changed);
    EXPECT_NE(second.error.find("already has an ACP"), std::string::npos);
    EXPECT_EQ(fixture.model.acps.size(), 1u);
    EXPECT_EQ(core::acp::CollectAcps(fixture.package).size(), 1u);
}

TEST(AcpEditingTest, CreatesConfidenceArgumentTreeForAcpAndLinksTopGoal) {
    AcpEditingFixture fixture;
    core::acp::AcpEditResult added = core::acp::AddAcp(fixture.model, &fixture.package, "relationship", "R1");
    ASSERT_TRUE(added.changed) << added.error;

    core::acp::AcpEditResult created =
        core::acp::CreateConfidenceArgumentTreeForAcp(fixture.model, &fixture.package, added.acp_id);
    ASSERT_TRUE(created.changed) << created.error;
    EXPECT_EQ(created.argument_package_id, "AP2");
    EXPECT_EQ(created.top_goal_id, "CC1");

    ASSERT_EQ(fixture.package.argumentPackages.size(), 2u);
    const sacm::ArgumentPackage& confidence_package = fixture.package.argumentPackages.back();
    EXPECT_TRUE(core::acp::IsConfidenceArgumentPackage(confidence_package));
    ASSERT_EQ(confidence_package.claims.size(), 1u);
    EXPECT_EQ(confidence_package.claims.front().id, "CC1");
    EXPECT_NE(confidence_package.claims.front().content.find("Confidence in"), std::string::npos);

    const parser::AcpRecord* acp = core::acp::FindAcp(fixture.model, added.acp_id);
    ASSERT_NE(acp, nullptr);
    EXPECT_EQ(acp->resolution_kind, "topGoalReference");
    EXPECT_EQ(acp->argument_package_id, "AP2");
    EXPECT_EQ(acp->top_goal_id, "CC1");

    auto parser_top_goal = std::find_if(fixture.model.elements.begin(), fixture.model.elements.end(), [](const auto& element) {
        return element.id == "CC1" && element.type == "claim";
    });
    EXPECT_NE(parser_top_goal, fixture.model.elements.end());

    const std::vector<core::acp::Acp> sacm_acps = core::acp::CollectAcps(fixture.package);
    ASSERT_EQ(sacm_acps.size(), 1u);
    EXPECT_EQ(sacm_acps.front().resolution.kind, core::acp::AcpResolutionKind::TopGoalReference);
    EXPECT_EQ(sacm_acps.front().resolution.argument_package_id, "AP2");
    EXPECT_EQ(sacm_acps.front().resolution.top_goal_id, "CC1");
}
