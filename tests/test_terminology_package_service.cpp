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
    ASSERT_TRUE(core::DeleteTerminologyPackage(package, created.package_ref, error)) << error;
    EXPECT_TRUE(package.terminologyPackages.empty());
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

    ASSERT_TRUE(core::DeleteTerminologyTerm(package, terminology_package.package_ref, created.term_ref, error)) << error;
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

TEST(TerminologyPackageService, ValidatesDuplicateMissingDescriptionAndNoCategory) {
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

    EXPECT_NE(std::find_if(issues.begin(), issues.end(), [](const core::TerminologyTermIssue& issue) {
                  return issue.severity == core::TerminologyTermIssueSeverity::Warning &&
                         issue.message.find("Duplicate") != std::string::npos;
              }),
              issues.end());
    EXPECT_NE(std::find_if(issues.begin(), issues.end(), [](const core::TerminologyTermIssue& issue) {
                  return issue.severity == core::TerminologyTermIssueSeverity::Warning &&
                         issue.message.find("description") != std::string::npos;
              }),
              issues.end());
    EXPECT_NE(std::find_if(issues.begin(), issues.end(), [](const core::TerminologyTermIssue& issue) {
                  return issue.severity == core::TerminologyTermIssueSeverity::Info &&
                         issue.message.find("category") != std::string::npos;
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
    EXPECT_EQ(core::CountTerminologyTermUsage(package, *term), 2);

    std::vector<core::TerminologyTermUsageSummary> summaries = core::BuildTerminologyTermUsageSummaries(
        package, *core::FindTerminologyPackage(package, terminology_package.package_ref));
    ASSERT_EQ(summaries.size(), 1u);
    EXPECT_EQ(summaries.front().count, 2);
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
    core::TerminologyPackageCreateResult terminology_package = core::CreateTerminologyPackage(package, "Vocabulary", "");
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

    const sacm::TerminologyPackage* reparsed = core::FindTerminologyPackage(parsed.package, terminology_package.package_ref);
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
