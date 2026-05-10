#include "core/terminology_package_service.h"
#include "sacm/sacm_parser.h"
#include "sacm/sacm_serializer.h"

#include <gtest/gtest.h>

#include <algorithm>
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

TEST(TerminologyPackageService, CreateAssignsUniqueIdGidAndEditableText) {
    sacm::AssuranceCasePackage package = MakePackage();
    sacm::TerminologyPackage existing;
    existing.id = "TP1";
    existing.gid = "gid-TP1";
    package.terminologyPackages.push_back(existing);

    core::TerminologyPackageCreateResult result =
        core::CreateTerminologyPackage(package, "Project Terms", "Terms used by this safety case.");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.package_ref.id, "TP2");
    EXPECT_EQ(result.package_ref.gid, "gid-TP2");
    ASSERT_EQ(package.terminologyPackages.size(), 2u);

    const sacm::TerminologyPackage* created = core::FindTerminologyPackage(package, result.package_ref);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->name, "Project Terms");
    EXPECT_EQ(created->name_ml.get_primary(), "Project Terms");
    EXPECT_EQ(created->description, "Terms used by this safety case.");
    EXPECT_EQ(created->description_ml.get_primary(), "Terms used by this safety case.");
}

TEST(TerminologyPackageService, UpdateEditsNameAndDescription) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult created = core::CreateTerminologyPackage(package, "Old", "Old description");
    ASSERT_TRUE(created.success);

    std::string error;
    ASSERT_TRUE(core::UpdateTerminologyPackage(package, created.package_ref, "New", "New description", error)) << error;

    const sacm::TerminologyPackage* updated = core::FindTerminologyPackage(package, created.package_ref);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->name, "New");
    EXPECT_EQ(updated->name_ml.get_primary(), "New");
    EXPECT_EQ(updated->description, "New description");
    EXPECT_EQ(updated->description_ml.get_primary(), "New description");
}

TEST(TerminologyPackageService, DeleteOnlyAllowsEmptyPackages) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult created = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(created.success);
    package.terminologyPackages.front().expressions.push_back(sacm::Expression{});

    std::string error;
    EXPECT_FALSE(core::DeleteTerminologyPackage(package, created.package_ref, error));
    EXPECT_EQ(package.terminologyPackages.size(), 1u);
    EXPECT_FALSE(error.empty());

    package.terminologyPackages.front().expressions.clear();
    package.terminologyPackages.front().terms.push_back(sacm::Term{});
    EXPECT_FALSE(core::DeleteTerminologyPackage(package, created.package_ref, error));
    EXPECT_EQ(package.terminologyPackages.size(), 1u);
    EXPECT_FALSE(error.empty());

    package.terminologyPackages.front().terms.clear();
    package.terminologyPackages.front().categories.push_back(sacm::Category{});
    EXPECT_FALSE(core::DeleteTerminologyPackage(package, created.package_ref, error));
    EXPECT_EQ(package.terminologyPackages.size(), 1u);
    EXPECT_FALSE(error.empty());

    package.terminologyPackages.front().categories.clear();
    ASSERT_TRUE(core::DeleteTerminologyPackage(package, created.package_ref, error)) << error;
    EXPECT_TRUE(package.terminologyPackages.empty());
}

TEST(TerminologyPackageService, CreateUpdateAndDeleteCategory) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyCategoryDraft draft;
    draft.name = "Operational Context";
    draft.description = "Terms describing the intended operating context.";
    core::TerminologyCategoryCreateResult created =
        core::CreateTerminologyCategory(package, terminology_package.package_ref, draft);
    ASSERT_TRUE(created.success) << created.error;
    EXPECT_EQ(created.category_ref.id, "CAT1");
    EXPECT_EQ(created.category_ref.gid, "gid-CAT1");

    const sacm::Category* category =
        core::FindTerminologyCategory(package, terminology_package.package_ref, created.category_ref);
    ASSERT_NE(category, nullptr);
    EXPECT_EQ(category->name, "Operational Context");
    EXPECT_EQ(category->description, "Terms describing the intended operating context.");

    core::TerminologyCategoryDraft update;
    update.name = "Operational Domain";
    update.description = "Updated category description.";
    std::string error;
    ASSERT_TRUE(
        core::UpdateTerminologyCategory(package, terminology_package.package_ref, created.category_ref, update, error))
        << error;

    category = core::FindTerminologyCategory(package, terminology_package.package_ref, created.category_ref);
    ASSERT_NE(category, nullptr);
    EXPECT_EQ(category->name, "Operational Domain");
    EXPECT_EQ(category->description, "Updated category description.");

    ASSERT_TRUE(core::DeleteTerminologyCategory(package, terminology_package.package_ref, created.category_ref, error))
        << error;
    const sacm::TerminologyPackage* terms = core::FindTerminologyPackage(package, terminology_package.package_ref);
    ASSERT_NE(terms, nullptr);
    EXPECT_TRUE(terms->categories.empty());
}

TEST(TerminologyPackageService, CategoryDeletionIsBlockedWhenAssignedToTerm) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyCategoryDraft category_draft;
    category_draft.name = "Hazard / Risk";
    core::TerminologyCategoryCreateResult category =
        core::CreateTerminologyCategory(package, terminology_package.package_ref, category_draft);
    ASSERT_TRUE(category.success);

    core::TerminologyTermDraft term_draft;
    term_draft.value = "hazard";
    term_draft.description = "A potential source of harm.";
    term_draft.category_refs = {category.category_ref.id};
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, term_draft).success);

    const sacm::TerminologyPackage* terms = core::FindTerminologyPackage(package, terminology_package.package_ref);
    ASSERT_NE(terms, nullptr);
    EXPECT_EQ(core::CountTermsUsingCategory(*terms, category.category_ref), 1);

    std::vector<core::TerminologyCategoryUsageSummary> summaries = core::BuildTerminologyCategoryUsageSummaries(*terms);
    ASSERT_EQ(summaries.size(), 1u);
    EXPECT_EQ(summaries.front().term_count, 1);
    EXPECT_EQ(core::CategoryDisplayName(*terms, category.category_ref.id), "Hazard / Risk");

    std::string error;
    EXPECT_FALSE(
        core::DeleteTerminologyCategory(package, terminology_package.package_ref, category.category_ref, error));
    EXPECT_FALSE(error.empty());
}

TEST(TerminologyPackageService, CreateUpdateAndDeleteTerm) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft draft;
    draft.value = "hazard";
    draft.name = "Hazard";
    draft.description = "A potential source of harm.";
    draft.category_refs = {"cat-risk", "cat-safety"};
    draft.externalReference = "ISO 26262";
    draft.origin = "Project glossary";

    core::TerminologyTermCreateResult created =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, draft);
    ASSERT_TRUE(created.success) << created.error;
    EXPECT_EQ(created.term_ref.id, "T1");
    EXPECT_EQ(created.term_ref.gid, "gid-T1");

    const sacm::Term* term = core::FindTerminologyTerm(package, terminology_package.package_ref, created.term_ref);
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->value, "hazard");
    EXPECT_EQ(term->name, "Hazard");
    EXPECT_EQ(term->description, "A potential source of harm.");
    EXPECT_EQ(term->category_refs.size(), 2u);
    EXPECT_EQ(term->externalReference, "ISO 26262");
    EXPECT_EQ(term->origin, "Project glossary");

    core::TerminologyTermDraft update = draft;
    update.value = "risk";
    update.name = "Risk";
    update.description = "Effect of uncertainty on objectives.";
    update.category_refs = {"cat-risk"};
    std::string error;
    ASSERT_TRUE(core::UpdateTerminologyTerm(package, terminology_package.package_ref, created.term_ref, update, error))
        << error;

    term = core::FindTerminologyTerm(package, terminology_package.package_ref, created.term_ref);
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->value, "risk");
    EXPECT_EQ(term->name, "Risk");
    EXPECT_EQ(term->description, "Effect of uncertainty on objectives.");
    ASSERT_EQ(term->category_refs.size(), 1u);
    EXPECT_EQ(term->category_refs.front(), "cat-risk");

    ASSERT_TRUE(core::DeleteTerminologyTerm(package, terminology_package.package_ref, created.term_ref, error))
        << error;
    const sacm::TerminologyPackage* terms = core::FindTerminologyPackage(package, terminology_package.package_ref);
    ASSERT_NE(terms, nullptr);
    EXPECT_TRUE(terms->terms.empty());
}

TEST(TerminologyPackageService, RejectsEmptyTermValue) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft draft;
    draft.value = "   ";
    core::TerminologyTermCreateResult created =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, draft);

    EXPECT_FALSE(created.success);
    EXPECT_FALSE(created.error.empty());
}

TEST(TerminologyPackageService, ValidatesMissingDescriptionCategoryAndExternalReference) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft first;
    first.value = "hazard";
    first.description = "A potential source of harm.";
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, first).success);

    core::TerminologyTermDraft second;
    second.value = "hazard";
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, second).success);

    const sacm::TerminologyPackage* terms = core::FindTerminologyPackage(package, terminology_package.package_ref);
    ASSERT_NE(terms, nullptr);
    std::vector<core::TerminologyTermIssue> issues = core::ValidateTerminologyTerms(*terms);

    EXPECT_EQ(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyTermIssue& issue) {
                               return issue.kind == core::TerminologyTermIssueKind::DuplicateDefinition;
                           }),
              issues.end());
    EXPECT_NE(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyTermIssue& issue) {
                               return issue.kind == core::TerminologyTermIssueKind::MissingDescription &&
                                      issue.severity == core::TerminologyTermIssueSeverity::Warning &&
                                      issue.message.find("description") != std::string::npos;
                           }),
              issues.end());
    EXPECT_NE(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyTermIssue& issue) {
                               return issue.kind == core::TerminologyTermIssueKind::MissingCategory &&
                                      issue.severity == core::TerminologyTermIssueSeverity::Info &&
                                      issue.message.find("category") != std::string::npos;
                           }),
              issues.end());
    EXPECT_NE(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyTermIssue& issue) {
                               return issue.kind == core::TerminologyTermIssueKind::MissingExternalReference &&
                                      issue.severity == core::TerminologyTermIssueSeverity::Info &&
                                      issue.message.find("external reference") != std::string::npos;
                           }),
              issues.end());
}

TEST(TerminologyPackageService, WarnsOnlyWhenDuplicateValueHasSameDefinition) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft operational_domain;
    operational_domain.value = "ODD";
    operational_domain.name = "Operational Design Domain";
    operational_domain.description = "Operating conditions for the system.";
    operational_domain.externalReference = "ISO 34503";
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, operational_domain).success);

    core::TerminologyTermDraft dataset;
    dataset.value = "ODD";
    dataset.name = "Object Detection Dataset";
    dataset.description = "Dataset used for object detection validation.";
    dataset.externalReference = "Project dataset catalog";
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, dataset).success);

    const sacm::TerminologyPackage* terms = core::FindTerminologyPackage(package, terminology_package.package_ref);
    ASSERT_NE(terms, nullptr);
    std::vector<core::TerminologyTermIssue> issues = core::ValidateTerminologyTerms(*terms);
    EXPECT_EQ(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyTermIssue& issue) {
                               return issue.kind == core::TerminologyTermIssueKind::DuplicateDefinition;
                           }),
              issues.end());

    core::TerminologyTermDraft repeated_definition;
    repeated_definition.value = "ODD";
    repeated_definition.name = "Repeated Operational Design Domain";
    repeated_definition.description = operational_domain.description;
    repeated_definition.externalReference = "ISO 34503";
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, repeated_definition).success);

    terms = core::FindTerminologyPackage(package, terminology_package.package_ref);
    ASSERT_NE(terms, nullptr);
    issues = core::ValidateTerminologyTerms(*terms);
    EXPECT_NE(std::find_if(issues.begin(),
                           issues.end(),
                           [](const core::TerminologyTermIssue& issue) {
                               return issue.kind == core::TerminologyTermIssueKind::DuplicateDefinition &&
                                      issue.severity == core::TerminologyTermIssueSeverity::Warning &&
                                      issue.message.find("definition") != std::string::npos;
                           }),
              issues.end());
}

TEST(TerminologyPackageService, CountsTermUsageInSacmText) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft draft;
    draft.value = "hazard";
    draft.description = "A potential source of harm.";
    core::TerminologyTermCreateResult created =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, draft);
    ASSERT_TRUE(created.success);

    sacm::ArgumentPackage argument_package;
    argument_package.name = "Hazard argument";
    sacm::Claim claim;
    claim.content = "The hazard is controlled. Hazardous is not a whole-word match.";
    argument_package.claims.push_back(claim);
    package.argumentPackages.push_back(argument_package);

    const sacm::Term* term = core::FindTerminologyTerm(package, terminology_package.package_ref, created.term_ref);
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(core::CountTerminologyTermUsage(package, *term), 1);

    std::vector<core::TerminologyTermUsageSummary> summaries = core::BuildTerminologyTermUsageSummaries(
        package, *core::FindTerminologyPackage(package, terminology_package.package_ref));
    ASSERT_EQ(summaries.size(), 1u);
    EXPECT_EQ(summaries.front().count, 1);
}

TEST(TerminologyPackageService, FindUsagesReportsNavigableArgumentElements) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    draft.name = "Operational Design Domain";
    core::TerminologyTermCreateResult term =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, draft);
    ASSERT_TRUE(term.success);

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    argument_package.gid = "gid-AP1";
    argument_package.name = "Main Argument";

    sacm::Claim goal;
    goal.id = "G1";
    goal.gid = "gid-G1";
    goal.name = "Top goal";
    goal.content = "The ODD is well defined.";
    argument_package.claims.push_back(goal);

    sacm::Claim assumption;
    assumption.id = "A1";
    assumption.assertionDeclaration = "assumed";
    assumption.content = "The ODD remains stable.";
    argument_package.claims.push_back(assumption);

    sacm::Claim justification;
    justification.id = "J1";
    justification.assertionDeclaration = "justification";
    justification.content = "The ODD definition is justified.";
    argument_package.claims.push_back(justification);

    sacm::ArgumentReasoning reasoning;
    reasoning.id = "S1";
    reasoning.content = "Argument over ODD completeness.";
    argument_package.argumentReasonings.push_back(reasoning);

    sacm::ArtifactReference context;
    context.id = "C1";
    context.description = "ODD definition from ISO 34503.";
    argument_package.artifactReferences.push_back(context);

    sacm::ArtifactReference solution;
    solution.id = "Sn1";
    solution.description = "ODD review evidence.";
    argument_package.artifactReferences.push_back(solution);

    sacm::ArtifactReference hidden_term_reference;
    hidden_term_reference.id = "AR1";
    hidden_term_reference.name = "ODD: Operational Design Domain";
    hidden_term_reference.referencedArtifact = term.term_ref.id;
    argument_package.artifactReferences.push_back(hidden_term_reference);

    sacm::AssertedContext asserted_context;
    asserted_context.sources = {"C1", "AR1"};
    asserted_context.targets = {"G1"};
    argument_package.assertedContexts.push_back(asserted_context);

    sacm::AssertedEvidence asserted_evidence;
    asserted_evidence.sources = {"Sn1"};
    asserted_evidence.targets = {"G1"};
    argument_package.assertedEvidences.push_back(asserted_evidence);

    package.argumentPackages.push_back(argument_package);

    core::TerminologyTermUsageSearchResult usages =
        core::FindTerminologyTermUsages(package, terminology_package.package_ref, term.term_ref);

    ASSERT_TRUE(usages.success) << usages.error;
    ASSERT_EQ(usages.usages.size(), 6u);
    const sacm::Term* found_term = core::FindTerminologyTerm(package, terminology_package.package_ref, term.term_ref);
    ASSERT_NE(found_term, nullptr);
    EXPECT_EQ(core::CountTerminologyTermUsage(package, *found_term), 6);
    std::vector<std::string> element_types;
    std::vector<std::string> element_ids;
    for (const auto& usage : usages.usages) {
        element_ids.push_back(usage.element_id);
        element_types.push_back(usage.element_type);
        EXPECT_EQ(usage.argument_package_name, "Main Argument");
        EXPECT_TRUE(usage.resolution_status == core::TerminologyUsageResolutionStatus::Resolved ||
                    usage.resolution_status == core::TerminologyUsageResolutionStatus::ExplicitContext);
        EXPECT_FALSE(usage.snippet.empty());
    }

    EXPECT_NE(std::find(element_types.begin(), element_types.end(), "Goal"), element_types.end());
    EXPECT_NE(std::find(element_types.begin(), element_types.end(), "Assumption"), element_types.end());
    EXPECT_NE(std::find(element_types.begin(), element_types.end(), "Justification"), element_types.end());
    EXPECT_NE(std::find(element_types.begin(), element_types.end(), "Strategy"), element_types.end());
    EXPECT_NE(std::find(element_types.begin(), element_types.end(), "Context"), element_types.end());
    EXPECT_NE(std::find(element_types.begin(), element_types.end(), "Solution"), element_types.end());
    EXPECT_EQ(std::find(element_ids.begin(), element_ids.end(), "AR1"), element_ids.end());
}

TEST(TerminologyPackageService, FindUsagesShowsAmbiguousAndExplicitStatuses) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft context_term;
    context_term.value = "ODD";
    context_term.name = "Operational Design Domain";
    core::TerminologyTermCreateResult context =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, context_term);
    ASSERT_TRUE(context.success);

    core::TerminologyTermDraft dataset_term;
    dataset_term.value = "ODD";
    dataset_term.name = "Object Detection Dataset";
    core::TerminologyTermCreateResult dataset =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, dataset_term);
    ASSERT_TRUE(dataset.success);

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::Claim claim;
    claim.id = "G1";
    claim.content = "The ODD is well defined.";
    argument_package.claims.push_back(claim);
    package.argumentPackages.push_back(argument_package);

    core::TerminologyTermUsageSearchResult ambiguous =
        core::FindTerminologyTermUsages(package, terminology_package.package_ref, context.term_ref);
    ASSERT_TRUE(ambiguous.success) << ambiguous.error;
    ASSERT_EQ(ambiguous.usages.size(), 1u);
    EXPECT_EQ(ambiguous.usages.front().resolution_status, core::TerminologyUsageResolutionStatus::Ambiguous);

    core::TerminologyContextAssociationResult association =
        core::AssociateTerminologyTermWithElement(package, "G1", terminology_package.package_ref, context.term_ref);
    ASSERT_TRUE(association.success) << association.error;

    core::TerminologyTermUsageSearchResult explicit_context =
        core::FindTerminologyTermUsages(package, terminology_package.package_ref, context.term_ref);
    ASSERT_TRUE(explicit_context.success) << explicit_context.error;
    ASSERT_EQ(explicit_context.usages.size(), 1u);
    EXPECT_EQ(explicit_context.usages.front().resolution_status,
              core::TerminologyUsageResolutionStatus::ExplicitContext);

    core::TerminologyTermUsageSearchResult other_meaning =
        core::FindTerminologyTermUsages(package, terminology_package.package_ref, dataset.term_ref);
    ASSERT_TRUE(other_meaning.success) << other_meaning.error;
    EXPECT_TRUE(other_meaning.usages.empty());

    const sacm::Term* context_term_ptr =
        core::FindTerminologyTerm(package, terminology_package.package_ref, context.term_ref);
    const sacm::Term* dataset_term_ptr =
        core::FindTerminologyTerm(package, terminology_package.package_ref, dataset.term_ref);
    ASSERT_NE(context_term_ptr, nullptr);
    ASSERT_NE(dataset_term_ptr, nullptr);
    EXPECT_EQ(core::CountTerminologyTermUsage(package, *context_term_ptr), 1);
    EXPECT_EQ(core::CountTerminologyTermUsage(package, *dataset_term_ptr), 0);

    const sacm::TerminologyPackage* terms = core::FindTerminologyPackage(package, terminology_package.package_ref);
    ASSERT_NE(terms, nullptr);
    std::vector<core::TerminologyTermUsageSummary> summaries =
        core::BuildTerminologyTermUsageSummaries(package, *terms);
    ASSERT_EQ(summaries.size(), 2u);
    EXPECT_EQ(summaries[0].count, 1);
    EXPECT_EQ(summaries[1].count, 0);
}

TEST(TerminologyPackageService, FindUsagesSplitsDuplicateAbbreviationsByExplicitMeaning) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft context_term;
    context_term.value = "ODD";
    context_term.name = "Operational Design Domain";
    core::TerminologyTermCreateResult context =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, context_term);
    ASSERT_TRUE(context.success);

    core::TerminologyTermDraft dataset_term;
    dataset_term.value = "ODD";
    dataset_term.name = "Object Detection Dataset";
    core::TerminologyTermCreateResult dataset =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, dataset_term);
    ASSERT_TRUE(dataset.success);

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::Claim operational_claim;
    operational_claim.id = "G1";
    operational_claim.content = "The ODD is well defined.";
    argument_package.claims.push_back(operational_claim);
    sacm::Claim dataset_claim;
    dataset_claim.id = "G2";
    dataset_claim.content = "The ODD is curated for perception review.";
    argument_package.claims.push_back(dataset_claim);
    package.argumentPackages.push_back(argument_package);

    ASSERT_TRUE(
        core::AssociateTerminologyTermWithElement(package, "G1", terminology_package.package_ref, context.term_ref)
            .success);
    ASSERT_TRUE(
        core::AssociateTerminologyTermWithElement(package, "G2", terminology_package.package_ref, dataset.term_ref)
            .success);

    core::TerminologyTermUsageSearchResult context_usages =
        core::FindTerminologyTermUsages(package, terminology_package.package_ref, context.term_ref);
    ASSERT_TRUE(context_usages.success) << context_usages.error;
    ASSERT_EQ(context_usages.usages.size(), 1u);
    EXPECT_EQ(context_usages.usages.front().element_id, "G1");
    EXPECT_EQ(context_usages.usages.front().resolution_status, core::TerminologyUsageResolutionStatus::ExplicitContext);

    core::TerminologyTermUsageSearchResult dataset_usages =
        core::FindTerminologyTermUsages(package, terminology_package.package_ref, dataset.term_ref);
    ASSERT_TRUE(dataset_usages.success) << dataset_usages.error;
    ASSERT_EQ(dataset_usages.usages.size(), 1u);
    EXPECT_EQ(dataset_usages.usages.front().element_id, "G2");
    EXPECT_EQ(dataset_usages.usages.front().resolution_status, core::TerminologyUsageResolutionStatus::ExplicitContext);
}

TEST(TerminologyPackageService, FindUsagesUsesWholeWordMatches) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Terms", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft draft;
    draft.value = "ODD";
    core::TerminologyTermCreateResult term =
        core::CreateTerminologyTerm(package, terminology_package.package_ref, draft);
    ASSERT_TRUE(term.success);

    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::Claim claim;
    claim.id = "G1";
    claim.content = "ODDity is not ODD.";
    argument_package.claims.push_back(claim);
    package.argumentPackages.push_back(argument_package);

    core::TerminologyTermUsageSearchResult usages =
        core::FindTerminologyTermUsages(package, terminology_package.package_ref, term.term_ref);

    ASSERT_TRUE(usages.success) << usages.error;
    ASSERT_EQ(usages.usages.size(), 1u);
    EXPECT_EQ(usages.usages.front().start_offset, 14u);
    EXPECT_EQ(usages.usages.front().matched_text, "ODD");
}

TEST(TerminologyPackageService, CreatedPackageSerializesAndParses) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult created =
        core::CreateTerminologyPackage(package, "Vocabulary", "Shared definitions.");
    ASSERT_TRUE(created.success);

    std::string xml = sacm::serialize_sacm(package);
    ASSERT_FALSE(xml.empty());

    sacm::SacmParseResult parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;

    const sacm::TerminologyPackage* reparsed = core::FindTerminologyPackage(parsed.package, created.package_ref);
    ASSERT_NE(reparsed, nullptr);
    EXPECT_EQ(reparsed->name, "Vocabulary");
    EXPECT_EQ(reparsed->description, "Shared definitions.");
}

TEST(TerminologyPackageService, CreatedTermsSerializeAndParse) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package =
        core::CreateTerminologyPackage(package, "Vocabulary", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyTermDraft first;
    first.value = "hazard";
    first.name = "Hazard";
    first.description = "A potential source of harm.";
    first.category_refs = {"cat-risk"};
    first.externalReference = "ISO 26262";
    first.origin = "Imported";
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, first).success);

    core::TerminologyTermDraft duplicate = first;
    duplicate.name = "Hazard Duplicate";
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, duplicate).success);

    std::string xml = sacm::serialize_sacm(package);
    ASSERT_FALSE(xml.empty());
    EXPECT_EQ(xml.find("definition"), std::string::npos);

    sacm::SacmParseResult parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;

    const sacm::TerminologyPackage* reparsed =
        core::FindTerminologyPackage(parsed.package, terminology_package.package_ref);
    ASSERT_NE(reparsed, nullptr);
    ASSERT_EQ(reparsed->terms.size(), 2u);
    EXPECT_EQ(reparsed->terms[0].value, "hazard");
    EXPECT_EQ(reparsed->terms[0].name, "Hazard");
    EXPECT_EQ(reparsed->terms[0].description, "A potential source of harm.");
    ASSERT_EQ(reparsed->terms[0].category_refs.size(), 1u);
    EXPECT_EQ(reparsed->terms[0].category_refs.front(), "cat-risk");
    EXPECT_EQ(reparsed->terms[0].externalReference, "ISO 26262");
    EXPECT_EQ(reparsed->terms[0].origin, "Imported");
    EXPECT_EQ(reparsed->terms[1].value, "hazard");
}

TEST(TerminologyPackageService, CreatedCategoriesSerializeAndParseWithTermAssignments) {
    sacm::AssuranceCasePackage package = MakePackage();
    core::TerminologyPackageCreateResult terminology_package =
        core::CreateTerminologyPackage(package, "Vocabulary", "");
    ASSERT_TRUE(terminology_package.success);

    core::TerminologyCategoryDraft category_draft;
    category_draft.name = "Operational Context";
    category_draft.description = "Operating conditions and boundaries.";
    core::TerminologyCategoryCreateResult category =
        core::CreateTerminologyCategory(package, terminology_package.package_ref, category_draft);
    ASSERT_TRUE(category.success);

    core::TerminologyTermDraft term;
    term.value = "ODD";
    term.name = "Operational Design Domain";
    term.description = "The operating conditions under which the system is intended to function.";
    term.category_refs = {category.category_ref.id};
    ASSERT_TRUE(core::CreateTerminologyTerm(package, terminology_package.package_ref, term).success);

    std::string xml = sacm::serialize_sacm(package);
    ASSERT_FALSE(xml.empty());

    sacm::SacmParseResult parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;

    const sacm::TerminologyPackage* reparsed =
        core::FindTerminologyPackage(parsed.package, terminology_package.package_ref);
    ASSERT_NE(reparsed, nullptr);
    ASSERT_EQ(reparsed->categories.size(), 1u);
    EXPECT_EQ(reparsed->categories.front().id, category.category_ref.id);
    EXPECT_EQ(reparsed->categories.front().name, "Operational Context");
    EXPECT_EQ(reparsed->categories.front().description, "Operating conditions and boundaries.");
    ASSERT_EQ(reparsed->terms.size(), 1u);
    ASSERT_EQ(reparsed->terms.front().category_refs.size(), 1u);
    EXPECT_EQ(reparsed->terms.front().category_refs.front(), category.category_ref.id);
}

TEST(TerminologyPackageService, VisibleTermContextCreatesArtifactReferenceAndAssertedContext) {
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

TEST(TerminologyPackageService, VisibleTermContextIsIdempotentPerTarget) {
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

TEST(TerminologyPackageService, VisibleTermContextUsesTargetScopedReferences) {
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

TEST(TerminologyPackageService, VisibleTermContextPromotesSingleHiddenAssociation) {
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

TEST(TerminologyPackageService, VisibleTermContextDoesNotPromoteSharedHiddenReference) {
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

TEST(TerminologyPackageService, VisibleTermContextSurvivesSaveReloadForClaimAndStrategy) {
    sacm::AssuranceCasePackage package = MakePackageWithTerminologyContextTargets();
    core::TerminologyPackageRef package_ref{"TP1", "gid-TP1"};
    core::TerminologyTermRef term_ref{"T_ODD", "gid-T_ODD"};
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "G1", package_ref, term_ref).success);
    ASSERT_TRUE(core::AddTerminologyTermAsVisibleContext(package, "S1", package_ref, term_ref).success);

    std::string xml = sacm::serialize_sacm(package);
    ASSERT_FALSE(xml.empty());

    sacm::SacmParseResult parsed = sacm::parse_sacm_string(xml);
    ASSERT_TRUE(parsed.success) << parsed.error_message;
    ASSERT_EQ(parsed.package.terminologyPackages.size(), 1u);
    ASSERT_EQ(parsed.package.terminologyPackages.front().terms.size(), 1u);
    ASSERT_EQ(parsed.package.argumentPackages.size(), 1u);
    const sacm::ArgumentPackage& argument_package = parsed.package.argumentPackages.front();
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

TEST(TerminologyPackageService, VisibleTermContextResolverFollowsMovedTermByStableId) {
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

TEST(TerminologyPackageService, VisibleTermContextValidationReportsMissingTerm) {
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

TEST(TerminologyPackageService, VisibleTermContextValidationReportsMissingSourceInvalidTargetAndDuplicate) {
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

TEST(TerminologyPackageService, VisibleTermContextExporterDoesNotDuplicateTermDefinition) {
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
