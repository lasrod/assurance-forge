#include "core/terminology_package_service.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

sacm::AssuranceCasePackage MakePackage() {
    sacm::AssuranceCasePackage package;
    package.id = "ACP1";
    package.name = "Assurance Case";
    package.name_ml.set("en", package.name);
    return package;
}

sacm::AssuranceCasePackage MakePackageWithTerminologyContextTargets() {
    sacm::AssuranceCasePackage package = MakePackage();

    sacm::TerminologyPackage terminology_package;
    terminology_package.id = "TP1";
    terminology_package.gid = "gid-TP1";
    terminology_package.name = "Glossary";
    sacm::Term term;
    term.id = "T_ODD";
    term.gid = "gid-T_ODD";
    term.value = "ODD";
    term.name = "Operational Design Domain";
    term.description = "The conditions under which the system is intended to operate.";
    term.description_ml.set("en", term.description);
    terminology_package.terms.push_back(term);
    package.terminologyPackages.push_back(terminology_package);

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    argument_package.gid = "gid-AP1";
    sacm::Claim first_goal;
    first_goal.id = "G1";
    first_goal.gid = "gid-G1";
    argument_package.claims.push_back(first_goal);
    sacm::Claim second_goal;
    second_goal.id = "G2";
    second_goal.gid = "gid-G2";
    argument_package.claims.push_back(second_goal);
    sacm::ArgumentReasoning strategy;
    strategy.id = "S1";
    strategy.gid = "gid-S1";
    argument_package.argumentReasonings.push_back(strategy);
    package.argumentPackages.push_back(argument_package);

    return package;
}

} // namespace

TEST(TerminologyVisibleTermContext, CreatesArtifactReferenceAndAssertedContext) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();

    core::TerminologyContextAssociationResult result = core::AddTerminologyTermAsVisibleContext(
        package, "G1", core::TerminologyPackageRef{"TP1", "gid-TP1"}, core::TerminologyTermRef{"T_ODD", "gid-T_ODD"});

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.created_artifact_reference);
    EXPECT_TRUE(result.created_asserted_context);
    ASSERT_EQ(package.argumentPackages.size(), 1u);
    const sacm::ArgumentPackage& argument_package = package.argumentPackages.front();
    ASSERT_EQ(argument_package.artifactReferences.size(), 1u);
    EXPECT_EQ(argument_package.artifactReferences.front().referencedArtifact, "T_ODD");
    EXPECT_TRUE(core::IsVisibleTerminologyArtifactReference(
        package, argument_package, argument_package.artifactReferences.front()));
    ASSERT_EQ(argument_package.assertedContexts.size(), 1u);
    EXPECT_TRUE(core::IsVisibleTerminologyContext(argument_package.assertedContexts.front()));
    EXPECT_EQ(argument_package.assertedContexts.front().sources,
              std::vector<std::string>{result.artifact_reference_id});
    EXPECT_EQ(argument_package.assertedContexts.front().targets, std::vector<std::string>{"G1"});
}

TEST(TerminologyVisibleTermContext, IsIdempotentPerTarget) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();

    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    core::TerminologyContextAssociationResult first =
        core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref);
    ASSERT_TRUE(first.success) << first.error;

    core::TerminologyContextAssociationResult second =
        core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref);

    ASSERT_TRUE(second.success) << second.error;
    EXPECT_TRUE(second.already_associated);
    EXPECT_EQ(second.artifact_reference_id, first.artifact_reference_id);
    EXPECT_EQ(second.asserted_context_id, first.asserted_context_id);
    EXPECT_EQ(package.argumentPackages.front().artifactReferences.size(), 1u);
    EXPECT_EQ(package.argumentPackages.front().assertedContexts.size(), 1u);
}

TEST(TerminologyVisibleTermContext, UsesTargetScopedReferences) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();

    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    core::TerminologyContextAssociationResult first =
        core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref);
    core::TerminologyContextAssociationResult second =
        core::AddTerminologyTermAsVisibleContext(package, "G2", package_ref, term_ref);

    ASSERT_TRUE(first.success) << first.error;
    ASSERT_TRUE(second.success) << second.error;
    EXPECT_NE(first.artifact_reference_id, second.artifact_reference_id);
    EXPECT_EQ(package.argumentPackages.front().artifactReferences.size(), 2u);
    EXPECT_EQ(package.argumentPackages.front().assertedContexts.size(), 2u);
}

TEST(TerminologyVisibleTermContext, PromotesSingleHiddenAssociation) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();

    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    core::TerminologyContextAssociationResult hidden =
        core::AssociateTerminologyTermWithElement(package, "G1", package_ref, term_ref);
    ASSERT_TRUE(hidden.success) << hidden.error;
    ASSERT_EQ(package.argumentPackages.front().artifactReferences.size(), 1u);
    ASSERT_EQ(package.argumentPackages.front().assertedContexts.size(), 1u);
    EXPECT_FALSE(core::IsVisibleTerminologyContext(package.argumentPackages.front().assertedContexts.front()));

    core::TerminologyContextAssociationResult visible =
        core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref);

    ASSERT_TRUE(visible.success) << visible.error;
    EXPECT_EQ(visible.artifact_reference_id, hidden.artifact_reference_id);
    EXPECT_EQ(visible.asserted_context_id, hidden.asserted_context_id);
    EXPECT_EQ(package.argumentPackages.front().artifactReferences.size(), 1u);
    ASSERT_EQ(package.argumentPackages.front().assertedContexts.size(), 1u);
    EXPECT_TRUE(core::IsVisibleTerminologyContext(package.argumentPackages.front().assertedContexts.front()));
}

TEST(TerminologyVisibleTermContext, DoesNotPromoteSharedHiddenReference) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();

    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    ASSERT_TRUE(core::AssociateTerminologyTermWithElement(package, "G1", package_ref, term_ref).success);
    ASSERT_TRUE(core::AssociateTerminologyTermWithElement(package, "G2", package_ref, term_ref).success);
    ASSERT_EQ(package.argumentPackages.front().artifactReferences.size(), 1u);
    ASSERT_EQ(package.argumentPackages.front().assertedContexts.size(), 2u);

    core::TerminologyContextAssociationResult visible =
        core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref);

    ASSERT_TRUE(visible.success) << visible.error;
    EXPECT_EQ(package.argumentPackages.front().artifactReferences.size(), 2u);
    ASSERT_EQ(package.argumentPackages.front().assertedContexts.size(), 2u);
    const auto visible_contexts = std::count_if(package.argumentPackages.front().assertedContexts.begin(),
                                                package.argumentPackages.front().assertedContexts.end(),
                                                core::IsVisibleTerminologyContext);
    EXPECT_EQ(visible_contexts, 1);
}

TEST(TerminologyVisibleTermContext, SurvivesSaveReloadForClaimAndStrategy) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();
    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref).success);
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "S1", package_ref, term_ref).success);

    std::string xml = sacm::serialize_sacm(package);
    ASSERT_FALSE(xml.empty());

    auto parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.has_value()) << (parsed ? "" : parsed.error());
    ASSERT_EQ(parsed->terminologyPackages.size(), 1u);
    ASSERT_EQ(parsed->terminologyPackages.front().terms.size(), 1u);
    ASSERT_EQ(parsed->argumentPackages.size(), 1u);
    const sacm::ArgumentPackage& argument_package = parsed->argumentPackages.front();
    ASSERT_EQ(argument_package.artifactReferences.size(), 2u);
    ASSERT_EQ(argument_package.assertedContexts.size(), 2u);
    EXPECT_EQ(std::count_if(argument_package.assertedContexts.begin(),
                            argument_package.assertedContexts.end(),
                            core::IsVisibleTerminologyContext),
              2);
    EXPECT_TRUE(std::all_of(argument_package.artifactReferences.begin(),
                            argument_package.artifactReferences.end(),
                            [](const sacm::ArtifactReference& artifact_reference) {
                                return artifact_reference.referencedArtifact == "T_ODD";
                            }));
}

TEST(TerminologyVisibleTermContext, ResolverFollowsMovedTermByStableId) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();
    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref).success);

    sacm::TerminologyPackage moved_package;
    moved_package.id = "TP2";
    moved_package.gid = "gid-TP2";
    moved_package.name = "Moved Glossary";
    moved_package.terms.push_back(package.terminologyPackages.front().terms.front());
    package.terminologyPackages.front().terms.clear();
    package.terminologyPackages.push_back(moved_package);

    core::TerminologyTermReferenceResolution resolution = core::ResolveTerminologyTermReference(package, "T_ODD");
    ASSERT_TRUE(resolution.resolved);
    EXPECT_EQ(resolution.package_ref.id, "TP2");
    EXPECT_EQ(resolution.term_ref.id, "T_ODD");
    EXPECT_TRUE(core::ValidateTerminologyContextReferences(package).empty());
}

TEST(TerminologyVisibleTermContext, ValidationReportsMissingTerm) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();
    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref).success);
    package.terminologyPackages.front().terms.clear();

    std::vector<core::TerminologyContextReferenceIssue> issues = core::ValidateTerminologyContextReferences(package);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues.front().kind, core::TerminologyContextReferenceIssueKind::MissingTerm);
    EXPECT_EQ(issues.front().severity, core::TerminologyTermIssueSeverity::Error);
    EXPECT_EQ(issues.front().referenced_artifact, "T_ODD");
}

TEST(TerminologyVisibleTermContext, ValidationReportsMissingSourceInvalidTargetAndDuplicate) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();
    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref).success);
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "G2", package_ref, term_ref).success);

    sacm::AssertedContext missing_source;
    missing_source.id = "AC_MISSING_SOURCE";
    missing_source.description = core::kVisibleTerminologyContextMarker;
    missing_source.sources = {"NO_SUCH_REF"};
    missing_source.targets = {"G1"};
    package.argumentPackages.front().assertedContexts.push_back(missing_source);

    sacm::AssertedContext invalid_target = package.argumentPackages.front().assertedContexts.front();
    invalid_target.id = "AC_INVALID_TARGET";
    invalid_target.targets = {"NO_SUCH_TARGET"};
    package.argumentPackages.front().assertedContexts.push_back(invalid_target);

    sacm::AssertedContext duplicate = package.argumentPackages.front().assertedContexts.front();
    duplicate.id = "AC_DUPLICATE";
    package.argumentPackages.front().assertedContexts.push_back(duplicate);

    std::vector<core::TerminologyContextReferenceIssue> issues = core::ValidateTerminologyContextReferences(package);
    EXPECT_NE(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyContextReferenceIssue& issue) {
                               return issue.kind ==
                                      core::TerminologyContextReferenceIssueKind::MissingArtifactReference;
                           }),
              issues.end());
    EXPECT_NE(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyContextReferenceIssue& issue) {
                               return issue.kind == core::TerminologyContextReferenceIssueKind::InvalidTarget;
                           }),
              issues.end());
    EXPECT_NE(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyContextReferenceIssue& issue) {
                               return issue.kind == core::TerminologyContextReferenceIssueKind::DuplicateContext;
                           }),
              issues.end());
}

TEST(TerminologyVisibleTermContext, ExporterDoesNotDuplicateTermDefinition) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();
    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref).success);

    const std::string definition = package.terminologyPackages.front().terms.front().description;
    std::string xml = sacm::serialize_sacm(package);
    ASSERT_FALSE(xml.empty());
    const std::size_t first = xml.find(definition);
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(xml.find(definition, first + definition.size()), std::string::npos);
}
