#include "core/terminology_scope_service.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <gtest/gtest.h>

#include <initializer_list>

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

    sacm::SacmParseResult parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;
    ASSERT_EQ(parsed.package.argumentPackages.size(), 1u);
    ASSERT_EQ(parsed.package.argumentPackages.front().terminologyPackages.size(), 1u);
    const sacm::TerminologyPackage& terminology_package =
        parsed.package.argumentPackages.front().terminologyPackages.front();
    EXPECT_EQ(terminology_package.id, "TP_ARG");
    ASSERT_EQ(terminology_package.terms.size(), 1u);
    EXPECT_EQ(terminology_package.terms.front().id, "T_ARG");
    EXPECT_EQ(terminology_package.terms.front().value, "ODD");
}