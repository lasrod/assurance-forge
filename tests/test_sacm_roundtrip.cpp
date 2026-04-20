#include <gtest/gtest.h>
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

// ===== Helpers for deep comparison =====

static void expect_base_eq(const sacm::SacmElement& a, const sacm::SacmElement& b,
                           const std::string& ctx) {
    EXPECT_EQ(a.id, b.id) << ctx << " id mismatch";
    EXPECT_EQ(a.name, b.name) << ctx << " name mismatch";
    EXPECT_EQ(a.description, b.description) << ctx << " description mismatch";
}

static void expect_rel_eq(const sacm::AssertedRelationship& a, const sacm::AssertedRelationship& b,
                          const std::string& ctx) {
    expect_base_eq(a, b, ctx);
    EXPECT_EQ(a.sources, b.sources) << ctx << " sources mismatch";
    EXPECT_EQ(a.targets, b.targets) << ctx << " targets mismatch";
    EXPECT_EQ(a.assertionDeclaration, b.assertionDeclaration) << ctx << " assertionDeclaration mismatch";
}

static void expect_packages_eq(const sacm::AssuranceCasePackage& a,
                                const sacm::AssuranceCasePackage& b) {
    expect_base_eq(a, b, "root");
    EXPECT_EQ(a.namespace_prefix, b.namespace_prefix);
    EXPECT_EQ(a.namespace_uri, b.namespace_uri);

    // Terminology packages
    ASSERT_EQ(a.terminologyPackages.size(), b.terminologyPackages.size());
    for (size_t i = 0; i < a.terminologyPackages.size(); ++i) {
        std::string ctx = "terminology[" + std::to_string(i) + "]";
        const auto& ta = a.terminologyPackages[i];
        const auto& tb = b.terminologyPackages[i];
        expect_base_eq(ta, tb, ctx);
        ASSERT_EQ(ta.expressions.size(), tb.expressions.size()) << ctx;
        for (size_t j = 0; j < ta.expressions.size(); ++j) {
            std::string ectx = ctx + ".expr[" + std::to_string(j) + "]";
            expect_base_eq(ta.expressions[j], tb.expressions[j], ectx);
            EXPECT_EQ(ta.expressions[j].value, tb.expressions[j].value) << ectx;
        }
    }

    // Artifact packages
    ASSERT_EQ(a.artifactPackages.size(), b.artifactPackages.size());
    for (size_t i = 0; i < a.artifactPackages.size(); ++i) {
        std::string ctx = "artifact_pkg[" + std::to_string(i) + "]";
        const auto& aa = a.artifactPackages[i];
        const auto& ab = b.artifactPackages[i];
        expect_base_eq(aa, ab, ctx);
        ASSERT_EQ(aa.artifacts.size(), ab.artifacts.size()) << ctx;
        for (size_t j = 0; j < aa.artifacts.size(); ++j) {
            expect_base_eq(aa.artifacts[j], ab.artifacts[j],
                           ctx + ".art[" + std::to_string(j) + "]");
        }
    }

    // Argument packages
    ASSERT_EQ(a.argumentPackages.size(), b.argumentPackages.size());
    for (size_t i = 0; i < a.argumentPackages.size(); ++i) {
        std::string ctx = "arg_pkg[" + std::to_string(i) + "]";
        const auto& pa = a.argumentPackages[i];
        const auto& pb = b.argumentPackages[i];
        expect_base_eq(pa, pb, ctx);

        // Claims
        ASSERT_EQ(pa.claims.size(), pb.claims.size()) << ctx << " claims";
        for (size_t j = 0; j < pa.claims.size(); ++j) {
            std::string cctx = ctx + ".claim[" + std::to_string(j) + "]";
            expect_base_eq(pa.claims[j], pb.claims[j], cctx);
            EXPECT_EQ(pa.claims[j].content, pb.claims[j].content) << cctx;
            EXPECT_EQ(pa.claims[j].assertionDeclaration, pb.claims[j].assertionDeclaration) << cctx;
        }

        // ArgumentReasonings
        ASSERT_EQ(pa.argumentReasonings.size(), pb.argumentReasonings.size()) << ctx << " reasonings";
        for (size_t j = 0; j < pa.argumentReasonings.size(); ++j) {
            std::string rctx = ctx + ".reasoning[" + std::to_string(j) + "]";
            expect_base_eq(pa.argumentReasonings[j], pb.argumentReasonings[j], rctx);
            EXPECT_EQ(pa.argumentReasonings[j].content, pb.argumentReasonings[j].content) << rctx;
        }

        // ArtifactReferences
        ASSERT_EQ(pa.artifactReferences.size(), pb.artifactReferences.size()) << ctx << " artRefs";
        for (size_t j = 0; j < pa.artifactReferences.size(); ++j) {
            std::string arctx = ctx + ".artRef[" + std::to_string(j) + "]";
            expect_base_eq(pa.artifactReferences[j], pb.artifactReferences[j], arctx);
            EXPECT_EQ(pa.artifactReferences[j].referencedArtifact,
                      pb.artifactReferences[j].referencedArtifact) << arctx;
        }

        // AssertedInferences
        ASSERT_EQ(pa.assertedInferences.size(), pb.assertedInferences.size()) << ctx << " inferences";
        for (size_t j = 0; j < pa.assertedInferences.size(); ++j) {
            std::string aictx = ctx + ".inf[" + std::to_string(j) + "]";
            expect_rel_eq(pa.assertedInferences[j], pb.assertedInferences[j], aictx);
            EXPECT_EQ(pa.assertedInferences[j].reasoning, pb.assertedInferences[j].reasoning) << aictx;
        }

        // AssertedContexts
        ASSERT_EQ(pa.assertedContexts.size(), pb.assertedContexts.size()) << ctx << " contexts";
        for (size_t j = 0; j < pa.assertedContexts.size(); ++j) {
            expect_rel_eq(pa.assertedContexts[j], pb.assertedContexts[j],
                          ctx + ".ctx[" + std::to_string(j) + "]");
        }

        // AssertedEvidences
        ASSERT_EQ(pa.assertedEvidences.size(), pb.assertedEvidences.size()) << ctx << " evidences";
        for (size_t j = 0; j < pa.assertedEvidences.size(); ++j) {
            expect_rel_eq(pa.assertedEvidences[j], pb.assertedEvidences[j],
                          ctx + ".ev[" + std::to_string(j) + "]");
        }
    }
}

// ===== File-based round-trip helper =====

static void round_trip_file(const std::string& file_path) {
    auto result1 = sacm::parse_sacm(file_path);
    ASSERT_TRUE(result1.success) << "Parse failed: " << result1.error_message;

    std::string xml = sacm::serialize_sacm(result1.package);
    ASSERT_FALSE(xml.empty()) << "Serialization produced empty string";

    auto result2 = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(result2.success) << "Re-parse failed: " << result2.error_message;

    expect_packages_eq(result1.package, result2.package);
}

// ===== Tests =====

TEST(SacmRoundTrip, SampleFile) {
    round_trip_file("data/sample.sacm.xml");
}

TEST(SacmRoundTrip, CoreArgumentFile) {
    round_trip_file("data/sacm_example_core_argument.xml");
}

TEST(SacmRoundTrip, OpenAutonomySafetyCase) {
    round_trip_file("data/open-autonomy-safety-case.sacm.xml");
}

TEST(SacmRoundTrip, SanitizedStrictFile) {
    round_trip_file("data/safety-case-sanitized-strict.sacm.xml");
}

TEST(SacmRoundTrip, UpdatedV2File) {
    round_trip_file("data/open-autonomy-safety-case.updated.v2.sacm.xml");
}

// ===== Parse correctness tests =====

TEST(SacmParser, SampleFileStructure) {
    auto result = sacm::parse_sacm("data/sample.sacm.xml");
    ASSERT_TRUE(result.success);

    const auto& pkg = result.package;
    EXPECT_EQ(pkg.id, "SAMPLE");
    EXPECT_EQ(pkg.name, "Sample Assurance Case");
    EXPECT_EQ(pkg.namespace_prefix, "sacm");

    // Terminology
    ASSERT_EQ(pkg.terminologyPackages.size(), 1u);
    EXPECT_EQ(pkg.terminologyPackages[0].id, "TP1");
    ASSERT_EQ(pkg.terminologyPackages[0].expressions.size(), 2u);
    EXPECT_EQ(pkg.terminologyPackages[0].expressions[0].id, "TERM_SAFE");
    EXPECT_EQ(pkg.terminologyPackages[0].expressions[0].value,
              "System operates without causing harm");

    // Argument package
    ASSERT_EQ(pkg.argumentPackages.size(), 1u);
    const auto& arg = pkg.argumentPackages[0];
    EXPECT_EQ(arg.claims.size(), 3u);
    EXPECT_EQ(arg.argumentReasonings.size(), 2u);
    EXPECT_EQ(arg.artifactReferences.size(), 2u);

    // Artifact package
    ASSERT_EQ(pkg.artifactPackages.size(), 1u);
    EXPECT_EQ(pkg.artifactPackages[0].artifacts.size(), 2u);
}

TEST(SacmParser, CoreArgumentFileStructure) {
    auto result = sacm::parse_sacm("data/sacm_example_core_argument.xml");
    ASSERT_TRUE(result.success);

    const auto& pkg = result.package;
    EXPECT_EQ(pkg.id, "acp_vehicle_braking");
    EXPECT_EQ(pkg.namespace_prefix, "SACM");

    ASSERT_EQ(pkg.argumentPackages.size(), 1u);
    const auto& arg = pkg.argumentPackages[0];
    EXPECT_EQ(arg.claims.size(), 4u);
    EXPECT_EQ(arg.argumentReasonings.size(), 1u);
    EXPECT_EQ(arg.artifactReferences.size(), 3u);
    EXPECT_EQ(arg.assertedInferences.size(), 2u);
    EXPECT_EQ(arg.assertedContexts.size(), 1u);
    EXPECT_EQ(arg.assertedEvidences.size(), 2u);

    // Check an inference has correct reasoning ref
    EXPECT_EQ(arg.assertedInferences[0].reasoning, "ar_1");
    EXPECT_EQ(arg.assertedInferences[0].sources.size(), 2u);
    EXPECT_EQ(arg.assertedInferences[0].targets.size(), 1u);
    EXPECT_EQ(arg.assertedInferences[0].targets[0], "cl_top");
}

TEST(SacmParser, NamespacePreservation) {
    // SACM: prefix (uppercase)
    auto r1 = sacm::parse_sacm("data/sacm_example_core_argument.xml");
    ASSERT_TRUE(r1.success);
    EXPECT_EQ(r1.package.namespace_prefix, "SACM");
    EXPECT_EQ(r1.package.namespace_uri, "http://example.org/sacm/2.3");

    // sacm: prefix (lowercase)
    auto r2 = sacm::parse_sacm("data/sample.sacm.xml");
    ASSERT_TRUE(r2.success);
    EXPECT_EQ(r2.package.namespace_prefix, "sacm");
    EXPECT_EQ(r2.package.namespace_uri, "http://www.omg.org/spec/SACM/2.2/Argumentation");
}

TEST(SacmParser, InvalidXmlReturnsError) {
    auto result = sacm::parse_sacm_string("<not valid xml");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST(SacmParser, NonExistentFileReturnsError) {
    auto result = sacm::parse_sacm("nonexistent_file.xml");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST(SacmParser, MissingRootReturnsError) {
    auto result = sacm::parse_sacm_string("<?xml version=\"1.0\"?><wrongRoot/>");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "Root element 'AssuranceCasePackage' not found");
}

TEST(SacmSerializer, ProducesValidXml) {
    sacm::AssuranceCasePackage pkg;
    pkg.id = "test";
    pkg.name = "Test Package";
    pkg.namespace_prefix = "sacm";
    pkg.namespace_uri = "http://www.omg.org/spec/SACM/2.2/Argumentation";

    sacm::Claim c;
    c.id = "C1";
    c.name = "Claim 1";
    c.content = "Test claim";
    c.description = "A test claim";

    sacm::ArgumentPackage ap;
    ap.id = "AP1";
    ap.claims.push_back(c);
    pkg.argumentPackages.push_back(ap);

    std::string xml = sacm::serialize_sacm(pkg);
    EXPECT_FALSE(xml.empty());
    EXPECT_NE(xml.find("sacm:AssuranceCasePackage"), std::string::npos);
    EXPECT_NE(xml.find("Test Package"), std::string::npos);
    EXPECT_NE(xml.find("Test claim"), std::string::npos);

    // Parse it back
    auto result = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.package.argumentPackages[0].claims[0].content, "Test claim");
}
