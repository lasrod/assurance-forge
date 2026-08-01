#include "core/acp/acp_editing.h"

#include "core/acp/assurance_claim_point.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>

namespace {

parser::SacmElement ParserElement(std::string id, std::string type, std::string assertion_declaration = {}) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    element.assertion_declaration = std::move(assertion_declaration);
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

sacm::Claim Claim(std::string id, std::string assertion_declaration) {
    sacm::Claim claim = Claim(std::move(id));
    claim.assertionDeclaration = std::move(assertion_declaration);
    return claim;
}

sacm::ArgumentReasoning Reasoning(std::string id) {
    sacm::ArgumentReasoning reasoning;
    reasoning.id = std::move(id);
    return reasoning;
}

sacm::ArtifactReference ArtifactReference(std::string id) {
    sacm::ArtifactReference artifact_reference;
    artifact_reference.id = std::move(id);
    return artifact_reference;
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
    edited.confidence_claim_id = "CC1";
    edited.argument_package_id = "StalePackage";
    edited.top_goal_id = "StaleTopGoal";
    core::acp::AcpEditResult updated = core::acp::UpsertAcp(fixture.model, &fixture.package, edited);
    ASSERT_TRUE(updated.changed) << updated.error;
    ASSERT_EQ(fixture.model.acps.size(), 1u);
    EXPECT_EQ(fixture.model.acps[0].text, edited.text);
    EXPECT_EQ(fixture.model.acps[0].confidence_claim_id, "CC1");
    EXPECT_TRUE(fixture.model.acps[0].argument_package_id.empty());
    EXPECT_TRUE(fixture.model.acps[0].top_goal_id.empty());

    const std::vector<core::acp::Acp> sacm_acps = core::acp::CollectAcps(fixture.package);
    ASSERT_EQ(sacm_acps.size(), 1u);
    EXPECT_EQ(sacm_acps[0].resolution.kind, core::acp::AcpResolutionKind::Text);
    EXPECT_EQ(sacm_acps[0].resolution.text, edited.text);
    EXPECT_EQ(sacm_acps[0].resolution.confidence_claim_id, "CC1");

    ASSERT_EQ(fixture.package.argumentPackages.size(), 1u);
    const sacm::ArgumentPackage& main_package = fixture.package.argumentPackages.front();
    auto text_claim = std::find_if(main_package.claims.begin(),
                                   main_package.claims.end(),
                                   [](const sacm::Claim& claim) { return claim.id == "CC1"; });
    ASSERT_NE(text_claim, main_package.claims.end());
    EXPECT_EQ(text_claim->content, edited.text);
    const sacm::AssertedInference& updated_inference =
        fixture.package.argumentPackages.front().assertedInferences.front();
    ASSERT_EQ(updated_inference.metaClaims.size(), 1u);
    EXPECT_EQ(updated_inference.metaClaims.front(), "CC1");

    core::acp::AcpEditResult removed = core::acp::RemoveAcp(fixture.model, &fixture.package, "ACP1");
    ASSERT_TRUE(removed.changed) << removed.error;
    EXPECT_TRUE(fixture.model.acps.empty());
    EXPECT_TRUE(core::acp::CollectAcps(fixture.package).empty());
    EXPECT_TRUE(fixture.package.argumentPackages.front().assertedInferences.front().metaClaims.empty());
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

TEST(AcpEditingTest, AllowsElementAcpOnlyOnArtefactReferences) {
    parser::AssuranceCase model;
    model.elements.push_back(ParserElement("G1", "claim"));
    model.elements.push_back(ParserElement("S1", "argumentreasoning"));
    model.elements.push_back(ParserElement("Sol1", "artifactreference"));
    model.elements.push_back(ParserElement("A1", "claim", "assumed"));
    model.elements.push_back(ParserElement("J1", "claim", "justification"));

    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    argument_package.claims.push_back(Claim("G1"));
    argument_package.argumentReasonings.push_back(Reasoning("S1"));
    argument_package.artifactReferences.push_back(ArtifactReference("Sol1"));
    argument_package.claims.push_back(Claim("A1", "assumed"));
    argument_package.claims.push_back(Claim("J1", "justification"));
    package.argumentPackages.push_back(std::move(argument_package));

    EXPECT_FALSE(core::acp::AddAcp(model, &package, "element", "G1").changed);
    EXPECT_FALSE(core::acp::AddAcp(model, &package, "element", "S1").changed);
    EXPECT_FALSE(core::acp::AddAcp(model, &package, "element", "A1").changed);
    EXPECT_FALSE(core::acp::AddAcp(model, &package, "element", "J1").changed);

    const core::acp::AcpEditResult solution_acp = core::acp::AddAcp(model, &package, "element", "Sol1");
    EXPECT_TRUE(solution_acp.changed) << solution_acp.error;
    ASSERT_EQ(model.acps.size(), 1u);
    EXPECT_EQ(model.acps.front().target_id, "Sol1");
}

TEST(AcpEditingTest, RejectsRelationshipAcpWhenRelationshipHasNoEligibleGsnEdge) {
    parser::AssuranceCase model;
    model.elements.push_back(ParserElement("S1", "argumentreasoning"));
    model.elements.push_back(ParserElement("G1", "claim"));
    model.elements.push_back(ParserRelationship("R1"));
    model.elements.back().target_refs = {"S1"};
    model.elements.back().source_refs = {"G1"};

    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    argument_package.argumentReasonings.push_back(Reasoning("S1"));
    argument_package.claims.push_back(Claim("G1"));
    sacm::AssertedInference inference;
    inference.id = "R1";
    inference.targets = {"S1"};
    inference.sources = {"G1"};
    argument_package.assertedInferences.push_back(std::move(inference));
    package.argumentPackages.push_back(std::move(argument_package));

    const core::acp::AcpEditResult result = core::acp::AddAcp(model, &package, "relationship", "R1");
    EXPECT_FALSE(result.changed);
    EXPECT_NE(result.error.find("Relationship ACP is only allowed"), std::string::npos);
    EXPECT_TRUE(model.acps.empty());
}

TEST(AcpEditingTest, CreatesConfidenceArgumentTreeForAcpAndLinksTopGoal) {
    AcpEditingFixture fixture;
    core::acp::AcpEditResult added = core::acp::AddAcp(fixture.model, &fixture.package, "relationship", "R1");
    ASSERT_TRUE(added.changed) << added.error;

    core::acp::AcpEditResult created =
        core::acp::CreateConfidenceArgumentTreeForAcp(fixture.model, &fixture.package, added.acp_id);
    ASSERT_TRUE(created.changed) << created.error;
    EXPECT_EQ(created.argument_package_id, "ACP1_AP");
    EXPECT_EQ(created.top_goal_id, "ACP1_G1");

    ASSERT_EQ(fixture.package.argumentPackages.size(), 2u);
    const sacm::ArgumentPackage& confidence_package = fixture.package.argumentPackages.back();
    EXPECT_TRUE(core::acp::IsConfidenceArgumentPackage(confidence_package));
    ASSERT_EQ(confidence_package.claims.size(), 1u);
    EXPECT_EQ(confidence_package.id, "ACP1_AP");
    EXPECT_EQ(confidence_package.claims.front().id, "ACP1_G1");
    EXPECT_NE(confidence_package.claims.front().content.find("Confidence in"), std::string::npos);

    const parser::AcpRecord* acp = core::acp::FindAcp(fixture.model, added.acp_id);
    ASSERT_NE(acp, nullptr);
    EXPECT_EQ(acp->resolution_kind, "topGoalReference");
    EXPECT_EQ(acp->argument_package_id, "ACP1_AP");
    EXPECT_EQ(acp->top_goal_id, "ACP1_G1");

    const sacm::AssertedInference& inference = fixture.package.argumentPackages.front().assertedInferences.front();
    ASSERT_EQ(inference.metaClaims.size(), 1u);
    EXPECT_EQ(inference.metaClaims.front(), "ACP1_G1");

    auto parser_top_goal =
        std::find_if(fixture.model.elements.begin(), fixture.model.elements.end(), [](const auto& element) {
            return element.id == "ACP1_G1" && element.type == "claim";
        });
    EXPECT_NE(parser_top_goal, fixture.model.elements.end());

    const std::vector<core::acp::Acp> sacm_acps = core::acp::CollectAcps(fixture.package);
    ASSERT_EQ(sacm_acps.size(), 1u);
    EXPECT_EQ(sacm_acps.front().resolution.kind, core::acp::AcpResolutionKind::TopGoalReference);
    EXPECT_EQ(sacm_acps.front().resolution.argument_package_id, "ACP1_AP");
    EXPECT_EQ(sacm_acps.front().resolution.top_goal_id, "ACP1_G1");

    const core::acp::AcpEditResult removed = core::acp::RemoveAcp(fixture.model, &fixture.package, added.acp_id);
    ASSERT_TRUE(removed.changed) << removed.error;
    EXPECT_TRUE(fixture.package.argumentPackages.front().assertedInferences.front().metaClaims.empty());
}

TEST(AcpEditingTest, PreservesDraftTextWhenCreatingConfidenceArgumentTree) {
    AcpEditingFixture fixture;
    core::acp::AcpEditResult added = core::acp::AddAcp(fixture.model, &fixture.package, "relationship", "R1");
    ASSERT_TRUE(added.changed) << added.error;

    parser::AcpRecord drafted = fixture.model.acps.front();
    drafted.resolution_kind = "text";
    drafted.text = "Draft confidence rationale that should survive mode changes.";
    drafted.confidence_claim_id = "CC1";
    core::acp::AcpEditResult drafted_result = core::acp::UpsertAcp(fixture.model, &fixture.package, drafted);
    ASSERT_TRUE(drafted_result.changed) << drafted_result.error;

    core::acp::AcpEditResult created =
        core::acp::CreateConfidenceArgumentTreeForAcp(fixture.model, &fixture.package, added.acp_id);
    ASSERT_TRUE(created.changed) << created.error;

    const parser::AcpRecord* acp = core::acp::FindAcp(fixture.model, added.acp_id);
    ASSERT_NE(acp, nullptr);
    EXPECT_EQ(acp->resolution_kind, "topGoalReference");
    EXPECT_EQ(acp->text, drafted.text);
    EXPECT_TRUE(acp->confidence_claim_id.empty());
    EXPECT_EQ(acp->argument_package_id, "ACP1_AP");
    EXPECT_EQ(acp->top_goal_id, "ACP1_G1");

    const std::vector<core::acp::Acp> sacm_acps = core::acp::CollectAcps(fixture.package);
    ASSERT_EQ(sacm_acps.size(), 1u);
    EXPECT_EQ(sacm_acps.front().resolution.kind, core::acp::AcpResolutionKind::TopGoalReference);
    EXPECT_EQ(sacm_acps.front().resolution.text, drafted.text);
    EXPECT_TRUE(sacm_acps.front().resolution.confidence_claim_id.empty());
}

TEST(AcpEditingTest, TopGoalReferenceWithoutCustomNamePreservesExistingConfidenceTreeLabels) {
    AcpEditingFixture fixture;
    core::acp::AcpEditResult added = core::acp::AddAcp(fixture.model, &fixture.package, "relationship", "R1");
    ASSERT_TRUE(added.changed) << added.error;
    core::acp::AcpEditResult created =
        core::acp::CreateConfidenceArgumentTreeForAcp(fixture.model, &fixture.package, added.acp_id);
    ASSERT_TRUE(created.changed) << created.error;

    ASSERT_EQ(fixture.package.argumentPackages.size(), 2u);
    sacm::ArgumentPackage& confidence_package = fixture.package.argumentPackages.back();
    ASSERT_EQ(confidence_package.claims.size(), 1u);
    confidence_package.name = "User confidence tree label";
    confidence_package.name_ml.set("en", confidence_package.name);
    confidence_package.claims.front().name = "User top goal label";
    confidence_package.claims.front().name_ml.set("en", confidence_package.claims.front().name);

    parser::AcpRecord edited = fixture.model.acps.front();
    edited.name.clear();
    edited.resolution_kind = "topGoalReference";
    edited.argument_package_id = created.argument_package_id;
    edited.top_goal_id = created.top_goal_id;
    core::acp::AcpEditResult updated = core::acp::UpsertAcp(fixture.model, &fixture.package, edited);
    ASSERT_TRUE(updated.changed) << updated.error;

    EXPECT_EQ(confidence_package.name, "User confidence tree label");
    EXPECT_EQ(confidence_package.name_ml.get("en"), "User confidence tree label");
    ASSERT_EQ(confidence_package.claims.size(), 1u);
    EXPECT_EQ(confidence_package.claims.front().name, "User top goal label");
    EXPECT_EQ(confidence_package.claims.front().name_ml.get("en"), "User top goal label");
}
