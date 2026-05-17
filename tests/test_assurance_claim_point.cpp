#include "core/acp/assurance_claim_point.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <gtest/gtest.h>

namespace {

sacm::AssuranceCasePackage MakePackageWithAcpOnRelationship() {
    sacm::AssuranceCasePackage package;
    package.id = "CASE1";
    package.name = "Case";
    package.namespace_prefix = "sacm";
    package.namespace_uri = "http://example.org/sacm/2.3";

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    argument_package.name = "Main Argument";

    sacm::Claim top;
    top.id = "G1";
    top.name = "Top goal";
    top.content = "The system is acceptably safe.";
    argument_package.claims.push_back(std::move(top));

    sacm::Claim child;
    child.id = "G2";
    child.name = "Child goal";
    child.content = "Hazards are controlled.";
    argument_package.claims.push_back(std::move(child));

    sacm::AssertedInference inference;
    inference.id = "R1";
    inference.sources.push_back("G2");
    inference.targets.push_back("G1");

    core::acp::Acp acp;
    acp.id = "ACP1";
    acp.target.kind = core::acp::AcpTargetKind::Relationship;
    acp.target.target_id = "R1";
    acp.resolution.kind = core::acp::AcpResolutionKind::Text;
    acp.resolution.text = "Confidence is justified by independent review.";
    core::acp::UpsertAcpTags(inference, acp);

    argument_package.assertedInferences.push_back(std::move(inference));
    package.argumentPackages.push_back(std::move(argument_package));
    return package;
}

} // namespace

TEST(AssuranceClaimPointTest, InstantiationIsDerivedFromResolutionKind) {
    core::acp::Acp acp;
    EXPECT_FALSE(core::acp::IsInstantiated(acp));

    acp.resolution.kind = core::acp::AcpResolutionKind::Text;
    EXPECT_TRUE(core::acp::IsInstantiated(acp));

    acp.resolution.kind = core::acp::AcpResolutionKind::TopGoalReference;
    EXPECT_TRUE(core::acp::IsInstantiated(acp));
}

TEST(AssuranceClaimPointTest, GeneratesNextAcpIdWithinArgumentPackage) {
    sacm::ArgumentPackage package;
    sacm::AssertedInference first;
    first.id = "R1";
    core::acp::Acp acp;
    acp.id = "ACP1";
    acp.target.kind = core::acp::AcpTargetKind::Relationship;
    acp.target.target_id = "R1";
    core::acp::UpsertAcpTags(first, acp);
    package.assertedInferences.push_back(std::move(first));

    EXPECT_EQ(core::acp::NextAcpId(package), "ACP2");
}

TEST(AssuranceClaimPointTest, RelationshipAcpRoundTripsThroughSacm) {
    const sacm::AssuranceCasePackage package = MakePackageWithAcpOnRelationship();
    const std::string xml = sacm::serialize_sacm(package);

    ASSERT_NE(xml.find("taggedValue"), std::string::npos);
    ASSERT_NE(xml.find("assuranceForge.acp"), std::string::npos);
    ASSERT_NE(xml.find("ACP1"), std::string::npos);

    sacm::SacmParseResult parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;
    ASSERT_EQ(parsed.package.argumentPackages.size(), 1u);

    const std::vector<core::acp::Acp> acps = core::acp::CollectAcps(parsed.package.argumentPackages.front());
    ASSERT_EQ(acps.size(), 1u);
    EXPECT_EQ(acps[0].id, "ACP1");
    EXPECT_EQ(acps[0].target.kind, core::acp::AcpTargetKind::Relationship);
    EXPECT_EQ(acps[0].target.target_id, "R1");
    EXPECT_EQ(acps[0].resolution.kind, core::acp::AcpResolutionKind::Text);
    EXPECT_EQ(acps[0].resolution.text, "Confidence is justified by independent review.");
    EXPECT_TRUE(core::acp::IsInstantiated(acps[0]));
}

TEST(AssuranceClaimPointTest, FlatParserProjectsSacmBackedAcpRecords) {
    const std::string xml = sacm::serialize_sacm(MakePackageWithAcpOnRelationship());

    parser::ParseResult parsed = parser::parse_sacm_xml_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;
    ASSERT_EQ(parsed.assurance_case.acps.size(), 1u);
    EXPECT_EQ(parsed.assurance_case.acps[0].id, "ACP1");
    EXPECT_EQ(parsed.assurance_case.acps[0].target_kind, "relationship");
    EXPECT_EQ(parsed.assurance_case.acps[0].target_id, "R1");
    EXPECT_EQ(parsed.assurance_case.acps[0].resolution_kind, "text");
    EXPECT_EQ(parsed.assurance_case.acps[0].text, "Confidence is justified by independent review.");
}

TEST(AssuranceClaimPointTest, ConfidenceArgumentPackagePurposeUsesSacmTaggedValue) {
    sacm::ArgumentPackage package;
    package.id = "Confidence";

    EXPECT_FALSE(core::acp::IsConfidenceArgumentPackage(package));
    core::acp::SetConfidenceArgumentPackage(package, true);
    EXPECT_TRUE(core::acp::IsConfidenceArgumentPackage(package));

    sacm::AssuranceCasePackage case_package;
    case_package.namespace_prefix = "sacm";
    case_package.namespace_uri = "http://example.org/sacm/2.3";
    case_package.argumentPackages.push_back(package);

    const std::string xml = sacm::serialize_sacm(case_package);
    ASSERT_NE(xml.find("assuranceForge.argumentPackage.purpose"), std::string::npos);

    sacm::SacmParseResult parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;
    ASSERT_EQ(parsed.package.argumentPackages.size(), 1u);
    EXPECT_TRUE(core::acp::IsConfidenceArgumentPackage(parsed.package.argumentPackages.front()));
}