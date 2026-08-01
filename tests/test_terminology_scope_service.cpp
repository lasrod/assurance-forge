#include "core/terminology_scope_service.h"
#include "core/terminology_package_service.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace {

sacm::Term MakeTerm(const std::string& id, const std::string& value, const std::string& name = {}) {
    sacm::Term term;
    term.id = id;
    term.gid = "gid-" + id;
    term.value = value;
    term.name = name;
    return term;
}

sacm::TerminologyPackage MakeTerminologyPackage(const std::string& id, std::initializer_list<sacm::Term> terms) {
    sacm::TerminologyPackage package;
    package.id = id;
    package.gid = "gid-" + id;
    package.name = id;
    package.terms.assign(terms.begin(), terms.end());
    return package;
}

sacm::AssuranceCasePackage MakePackageWithArgument() {
    sacm::AssuranceCasePackage package;
    package.id = "ACP1";
    package.gid = "gid-ACP1";

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    argument_package.gid = "gid-AP1";

    sacm::Claim claim;
    claim.id = "G1";
    claim.gid = "gid-G1";
    claim.content = "The ODD is well defined.";
    argument_package.claims.push_back(claim);

    package.argumentPackages.push_back(argument_package);
    return package;
}

} // namespace

TEST(TerminologyScopeService, ActiveTermsUseDeterministicScopeOrder) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.argumentPackages.front().terminologyPackages.push_back(
        MakeTerminologyPackage("TP_ARG", {MakeTerm("T_ARG", "ODD", "Argument term")}));
    package.terminologyPackages.push_back(MakeTerminologyPackage("TP_CASE", {MakeTerm("T_CASE", "ODD", "Case term")}));

    core::TerminologyService service(package);
    core::TerminologyScopeContext scope;
    scope.element_id = "G1";
    scope.argument_package_ref.id = "AP1";

    std::vector<core::TerminologyScopedTermRef> active_terms = service.GetActiveTermsForElement(scope);

    ASSERT_EQ(active_terms.size(), 2u);
    EXPECT_EQ(active_terms[0].term_ref.id, "T_ARG");
    EXPECT_EQ(active_terms[0].layer, core::TerminologyLookupLayer::ArgumentPackageTerminology);
    EXPECT_EQ(active_terms[1].term_ref.id, "T_CASE");
    EXPECT_EQ(active_terms[1].layer, core::TerminologyLookupLayer::AssuranceCaseTerminology);
}

TEST(TerminologyScopeService, ExplicitContextTermsPrecedePackageTerms) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(MakeTerminologyPackage("TP_CASE", {MakeTerm("T_CONTEXT", "ODD")}));
    package.argumentPackages.front().terminologyPackages.push_back(
        MakeTerminologyPackage("TP_ARG", {MakeTerm("T_ARG", "ODD")}));

    sacm::ArtifactReference artifact_reference;
    artifact_reference.id = "AR1";
    artifact_reference.gid = "gid-AR1";
    artifact_reference.referencedArtifact = "T_CONTEXT";
    package.argumentPackages.front().artifactReferences.push_back(artifact_reference);

    sacm::AssertedContext context;
    context.id = "AC1";
    context.sources = {"AR1"};
    context.targets = {"G1"};
    package.argumentPackages.front().assertedContexts.push_back(context);

    core::TerminologyService service(package);
    core::TerminologyScopeContext scope;
    scope.element_id = "G1";
    scope.argument_package_ref.id = "AP1";

    std::vector<core::TerminologyScopedTermRef> active_terms = service.GetActiveTermsForElement(scope);

    ASSERT_GE(active_terms.size(), 2u);
    EXPECT_EQ(active_terms[0].term_ref.id, "T_CONTEXT");
    EXPECT_EQ(active_terms[0].layer, core::TerminologyLookupLayer::ExplicitElementContext);
    EXPECT_EQ(active_terms[1].term_ref.id, "T_ARG");
}

TEST(TerminologyScopeService, UniqueAndAmbiguousResolutionAreDistinct) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(MakeTerminologyPackage("TP_CASE", {MakeTerm("T1", "ODD")}));

    core::TerminologyService service(package);
    core::TerminologyScopeContext scope = service.BuildScopeContextForElement("G1");
    core::TextOccurrence occurrence;
    occurrence.element_id = "G1";
    occurrence.text = "ODD";

    core::TermResolution resolution = service.ResolveOccurrence(occurrence, scope);
    ASSERT_EQ(resolution.status, core::TermResolutionStatus::Unique);
    ASSERT_TRUE(resolution.selected.has_value());
    EXPECT_EQ(resolution.selected->term_ref.id, "T1");

    package.terminologyPackages.front().terms.push_back(MakeTerm("T2", "ODD"));
    core::TerminologyService ambiguous_service(package);
    resolution = ambiguous_service.ResolveOccurrence(occurrence, ambiguous_service.BuildScopeContextForElement("G1"));

    EXPECT_EQ(resolution.status, core::TermResolutionStatus::Ambiguous);
    EXPECT_FALSE(resolution.selected.has_value());
    EXPECT_EQ(resolution.candidates.size(), 2u);
}

TEST(TerminologyScopeService, ExplicitBindingOverridesAmbiguity) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(
        MakeTerminologyPackage("TP_CASE", {MakeTerm("T1", "ODD"), MakeTerm("T2", "ODD")}));

    core::TerminologyService service(package);
    core::TextOccurrence occurrence;
    occurrence.element_id = "G1";
    occurrence.text = "ODD";
    occurrence.has_explicit_term_ref = true;
    occurrence.explicit_term_ref.id = "T2";

    core::TermResolution resolution = service.ResolveOccurrence(occurrence, service.BuildScopeContextForElement("G1"));

    ASSERT_EQ(resolution.status, core::TermResolutionStatus::Explicit);
    ASSERT_TRUE(resolution.selected.has_value());
    EXPECT_EQ(resolution.selected->term_ref.id, "T2");
}

TEST(TerminologyScopeService, IgnoredAndUndefinedImportantOccurrencesAreReported) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    core::TerminologyService service(package);
    core::TerminologyScopeContext scope = service.BuildScopeContextForElement("G1");

    core::TextOccurrence ignored;
    ignored.text = "ODD";
    ignored.ignored = true;
    core::TermResolution resolution = service.ResolveOccurrence(ignored, scope);
    EXPECT_EQ(resolution.status, core::TermResolutionStatus::Ignored);

    core::TextOccurrence undefined;
    undefined.text = "ODD";
    resolution = service.ResolveOccurrence(undefined, scope);
    EXPECT_EQ(resolution.status, core::TermResolutionStatus::None);
    EXPECT_TRUE(resolution.important_undefined);

    undefined.text = "ordinary";
    resolution = service.ResolveOccurrence(undefined, scope);
    EXPECT_EQ(resolution.status, core::TermResolutionStatus::None);
    EXPECT_FALSE(resolution.important_undefined);
}

TEST(TerminologyScopeService, ArgumentPackageTerminologyRoundTrips) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.argumentPackages.front().terminologyPackages.push_back(
        MakeTerminologyPackage("TP_ARG", {MakeTerm("T_ARG", "ODD", "Operational Design Domain")}));

    std::string xml = sacm::serialize_sacm(package);
    ASSERT_FALSE(xml.empty());

    auto parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.has_value()) << (parsed ? "" : parsed.error());
    ASSERT_EQ(parsed->argumentPackages.size(), 1u);
    ASSERT_EQ(parsed->argumentPackages.front().terminologyPackages.size(), 1u);
    const sacm::TerminologyPackage& terminology_package = parsed->argumentPackages.front().terminologyPackages.front();
    EXPECT_EQ(terminology_package.id, "TP_ARG");
    ASSERT_EQ(terminology_package.terms.size(), 1u);
    EXPECT_EQ(terminology_package.terms.front().id, "T_ARG");
    EXPECT_EQ(terminology_package.terms.front().value, "ODD");
}

TEST(TerminologyScopeService, DetectsKnownTermsInVisibleText) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(MakeTerminologyPackage("TP_CASE", {MakeTerm("T_ODD", "ODD")}));

    core::TerminologyService service(package);
    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("G1", "The ODD is well defined.");

    ASSERT_EQ(occurrences.size(), 1u);
    EXPECT_EQ(occurrences.front().kind, core::TermOccurrenceKind::KnownTerm);
    EXPECT_EQ(occurrences.front().start_offset, 4u);
    EXPECT_EQ(occurrences.front().end_offset, 7u);
    EXPECT_EQ(occurrences.front().text, "ODD");
    ASSERT_EQ(occurrences.front().resolution.status, core::TermResolutionStatus::Unique);
    ASSERT_TRUE(occurrences.front().resolution.selected.has_value());
    EXPECT_EQ(occurrences.front().resolution.selected->term_ref.id, "T_ODD");
}

TEST(TerminologyScopeService, DetectionUsesWholeWordBoundaries) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(MakeTerminologyPackage("TP_CASE", {MakeTerm("T_ODD", "ODD")}));

    core::TerminologyService service(package);
    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("G1", "The ODDity is not relevant.");

    EXPECT_TRUE(occurrences.empty());
}

TEST(TerminologyScopeService, DetectionSupportsMultiWordTermsAndLongestMatch) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(MakeTerminologyPackage(
        "TP_CASE", {MakeTerm("T_SHORT", "Operational Design"), MakeTerm("T_LONG", "Operational Design Domain")}));

    core::TerminologyService service(package);
    std::vector<core::TermOccurrence> occurrences =
        service.DetectTermsInText("G1", "Operational Design Domain is defined.");

    ASSERT_EQ(occurrences.size(), 1u);
    EXPECT_EQ(occurrences.front().text, "Operational Design Domain");
    ASSERT_EQ(occurrences.front().resolution.status, core::TermResolutionStatus::Unique);
    ASSERT_TRUE(occurrences.front().resolution.selected.has_value());
    EXPECT_EQ(occurrences.front().resolution.selected->term_ref.id, "T_LONG");
}

TEST(TerminologyScopeService, DetectionMarksDuplicateTermValuesAmbiguous) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(
        MakeTerminologyPackage("TP_CASE", {MakeTerm("T_CONTEXT", "ODD"), MakeTerm("T_DATASET", "ODD")}));

    core::TerminologyService service(package);
    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("G1", "The ODD is well defined.");

    ASSERT_EQ(occurrences.size(), 1u);
    EXPECT_EQ(occurrences.front().kind, core::TermOccurrenceKind::KnownTerm);
    EXPECT_EQ(occurrences.front().resolution.status, core::TermResolutionStatus::Ambiguous);
    EXPECT_FALSE(occurrences.front().resolution.selected.has_value());
    EXPECT_EQ(occurrences.front().resolution.candidates.size(), 2u);
}

TEST(TerminologyScopeService, ExplicitTermContextResolvesDuplicateTermValueForElement) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(
        MakeTerminologyPackage("TP_CASE",
                               {MakeTerm("T_CONTEXT", "ODD", "Operational Design Domain"),
                                MakeTerm("T_DATASET", "ODD", "Object Detection Dataset")}));

    core::TerminologyContextAssociationResult association =
        core::AssociateTerminologyTermWithElement(package,
                                                  "G1",
                                                  core::TerminologyPackageRef{"TP_CASE", "gid-TP_CASE"},
                                                  core::TerminologyTermRef{"T_DATASET", "gid-T_DATASET"});
    ASSERT_TRUE(association.success) << association.error;

    core::TerminologyService service(package);
    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("G1", "The ODD is well defined.");

    ASSERT_EQ(occurrences.size(), 1u);
    EXPECT_EQ(occurrences.front().resolution.status, core::TermResolutionStatus::Explicit);
    ASSERT_TRUE(occurrences.front().resolution.selected.has_value());
    EXPECT_EQ(occurrences.front().resolution.selected->term_ref.id, "T_DATASET");
}

TEST(TerminologyScopeService, ExplicitTermContextResolvesDuplicateTermValueForSolution) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    sacm::ArtifactReference solution;
    solution.id = "Sn1";
    solution.gid = "gid-Sn1";
    solution.description = "The ODD evidence is reviewed.";
    package.argumentPackages.front().artifactReferences.push_back(solution);
    package.terminologyPackages.push_back(
        MakeTerminologyPackage("TP_CASE",
                               {MakeTerm("T_CONTEXT", "ODD", "Operational Design Domain"),
                                MakeTerm("T_DATASET", "ODD", "Object Detection Dataset")}));

    core::TerminologyContextAssociationResult association =
        core::AssociateTerminologyTermWithElement(package,
                                                  "Sn1",
                                                  core::TerminologyPackageRef{"TP_CASE", "gid-TP_CASE"},
                                                  core::TerminologyTermRef{"T_CONTEXT", "gid-T_CONTEXT"});
    ASSERT_TRUE(association.success) << association.error;

    core::TerminologyService service(package);
    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("Sn1", "The ODD evidence is reviewed.");

    ASSERT_EQ(occurrences.size(), 1u);
    EXPECT_EQ(occurrences.front().resolution.status, core::TermResolutionStatus::Explicit);
    ASSERT_TRUE(occurrences.front().resolution.selected.has_value());
    EXPECT_EQ(occurrences.front().resolution.selected->term_ref.id, "T_CONTEXT");
}

TEST(TerminologyScopeService, TermContextAssociationIsIdempotent) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(
        MakeTerminologyPackage("TP_CASE", {MakeTerm("T_CONTEXT", "ODD", "Operational Design Domain")}));

    core::TerminologyPackageRef package_ref{"TP_CASE", "gid-TP_CASE"};
    core::TerminologyTermRef term_ref{"T_CONTEXT", "gid-T_CONTEXT"};
    core::TerminologyContextAssociationResult first =
        core::AssociateTerminologyTermWithElement(package, "G1", package_ref, term_ref);
    core::TerminologyContextAssociationResult second =
        core::AssociateTerminologyTermWithElement(package, "G1", package_ref, term_ref);

    ASSERT_TRUE(first.success) << first.error;
    ASSERT_TRUE(second.success) << second.error;
    EXPECT_FALSE(first.already_associated);
    EXPECT_TRUE(second.already_associated);
    ASSERT_EQ(package.argumentPackages.size(), 1u);
    EXPECT_EQ(package.argumentPackages.front().artifactReferences.size(), 1u);
    EXPECT_EQ(package.argumentPackages.front().assertedContexts.size(), 1u);
}

TEST(TerminologyScopeService, TermContextAssociationSurvivesRoundTripAndResolvesAmbiguity) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(
        MakeTerminologyPackage("TP_CASE",
                               {MakeTerm("T_CONTEXT", "ODD", "Operational Design Domain"),
                                MakeTerm("T_DATASET", "ODD", "Object Detection Dataset")}));
    ASSERT_TRUE(core::AssociateTerminologyTermWithElement(package,
                                                          "G1",
                                                          core::TerminologyPackageRef{"TP_CASE", "gid-TP_CASE"},
                                                          core::TerminologyTermRef{"T_CONTEXT", "gid-T_CONTEXT"})
                    .success);

    std::string xml = sacm::serialize_sacm(package);
    auto parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.has_value()) << (parsed ? "" : parsed.error());
    ASSERT_EQ(parsed->argumentPackages.size(), 1u);
    EXPECT_EQ(parsed->argumentPackages.front().artifactReferences.size(), 1u);
    EXPECT_EQ(parsed->argumentPackages.front().assertedContexts.size(), 1u);

    core::TerminologyService service(*parsed);
    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("G1", "The ODD is well defined.");

    ASSERT_EQ(occurrences.size(), 1u);
    EXPECT_EQ(occurrences.front().resolution.status, core::TermResolutionStatus::Explicit);
    ASSERT_TRUE(occurrences.front().resolution.selected.has_value());
    EXPECT_EQ(occurrences.front().resolution.selected->term_ref.id, "T_CONTEXT");
}

TEST(TerminologyScopeService, DetectionReportsUndefinedAcronyms) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    core::TerminologyService service(package);

    std::vector<core::TermOccurrence> occurrences =
        service.DetectTermsInText("G1", "ODD, HARA, FMEA and ASIL are assessed.");

    ASSERT_EQ(occurrences.size(), 4u);
    EXPECT_EQ(occurrences[0].text, "ODD");
    EXPECT_EQ(occurrences[1].text, "HARA");
    EXPECT_EQ(occurrences[2].text, "FMEA");
    EXPECT_EQ(occurrences[3].text, "ASIL");
    for (const auto& occurrence : occurrences) {
        EXPECT_EQ(occurrence.kind, core::TermOccurrenceKind::UndefinedAcronym);
        EXPECT_EQ(occurrence.resolution.status, core::TermResolutionStatus::None);
        EXPECT_TRUE(occurrence.resolution.important_undefined);
    }
}

TEST(TerminologyScopeService, DetectionIgnoresLowercaseAndMixedCaseUnknownWords) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    core::TerminologyService service(package);

    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("G1", "odd Hara ASIL");

    ASSERT_EQ(occurrences.size(), 1u);
    EXPECT_EQ(occurrences.front().text, "ASIL");
    EXPECT_EQ(occurrences.front().kind, core::TermOccurrenceKind::UndefinedAcronym);
}

TEST(TerminologyScopeService, DetectionDoesNotDuplicateKnownAcronymAsUndefined) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.terminologyPackages.push_back(MakeTerminologyPackage("TP_CASE", {MakeTerm("T_ODD", "ODD")}));

    core::TerminologyService service(package);
    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("G1", "ODD and HARA are assessed.");

    ASSERT_EQ(occurrences.size(), 2u);
    EXPECT_EQ(occurrences[0].text, "ODD");
    EXPECT_EQ(occurrences[0].kind, core::TermOccurrenceKind::KnownTerm);
    EXPECT_EQ(occurrences[1].text, "HARA");
    EXPECT_EQ(occurrences[1].kind, core::TermOccurrenceKind::UndefinedAcronym);
}

TEST(TerminologyScopeService, DetectionUsesArgumentPackageScope) {
    sacm::AssuranceCasePackage package = MakePackageWithArgument();
    package.argumentPackages.front().terminologyPackages.push_back(
        MakeTerminologyPackage("TP_ARG", {MakeTerm("T_ARG", "ODD")}));

    core::TerminologyService service(package);
    std::vector<core::TermOccurrence> occurrences = service.DetectTermsInText("G1", "The ODD is well defined.");

    ASSERT_EQ(occurrences.size(), 1u);
    ASSERT_EQ(occurrences.front().resolution.status, core::TermResolutionStatus::Unique);
    ASSERT_TRUE(occurrences.front().resolution.selected.has_value());
    EXPECT_EQ(occurrences.front().resolution.selected->term_ref.id, "T_ARG");
    EXPECT_EQ(occurrences.front().resolution.selected->layer, core::TerminologyLookupLayer::ArgumentPackageTerminology);
}
