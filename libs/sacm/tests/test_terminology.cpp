#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace {

using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CreateCategory;
using sacm::commands::CreateExpression;
using sacm::commands::CreateTerm;
using sacm::commands::CreateTerminologyPackage;
using sacm::commands::DeleteElement;
using sacm::commands::ReferenceDeletePolicy;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::model::Document;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

TEST(Sacm23Terminology, SACM23_TERM_001_TerminologyHeavyFixtureRoundTrips) {
    const LoadResult first = sacm::io::load_xmi_file(fixture("terminology-full-valid.sacm.xmi"),
                                                     LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(first.ok) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    const auto& document = *first.document;

    const auto* term = document.find_as<sacm::model::Term>(ElementId{"term_iso26262"});
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->value(), "ISO 26262");
    EXPECT_EQ(term->external_reference(), "https://www.iso.org/standard/68383.html");
    ASSERT_EQ(term->categories().size(), 1u);
    EXPECT_EQ(term->categories().front().value(), "cat_iso");

    const auto* category = document.find_as<sacm::model::Category>(ElementId{"cat_standards"});
    ASSERT_NE(category, nullptr);
    ASSERT_EQ(category->categories().size(), 1u);  // SACM 2.3 sub-category

    const auto* expression = document.find_as<sacm::model::Expression>(ElementId{"expr_1"});
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(expression->elements().size(), 2u);

    const auto* group = document.find_as<sacm::model::TerminologyGroup>(ElementId{"group_1"});
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->terminology_elements().size(), 2u);

    // ExpressionLangString: claim description points into the terminology.
    const auto* claim = document.find_as<sacm::model::Claim>(ElementId{"claim_1"});
    ASSERT_NE(claim, nullptr);
    ASSERT_EQ(claim->description().values.size(), 1u);
    ASSERT_TRUE(claim->description().values.front().expression_ref.has_value());
    EXPECT_EQ(claim->description().values.front().expression_ref->value(), "expr_1");

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult second = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *second.document).empty());
}

TEST(Sacm23Terminology, SACM23_TERM_001_CategoryReferenceToNonCategoryIsInvalid) {
    const LoadResult result =
        sacm::io::load_xmi_file(fixture("invalid/category-ref-wrong-type-invalid.sacm.xmi"),
                                LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value());
    const auto diagnostics = sacm::validation::validate(*result.document);
    EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kRefWrongType;
    }));
}

TEST(Sacm23Terminology, SACM23_TERM_001_CreatesAndDeletesTerminologyElements) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateTerminologyPackage{
                        .parent = ElementId{"acp_1"}, .id = ElementId{"termpkg_1"}, .name = "Vocab"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateCategory{.parent = ElementId{"termpkg_1"},
                                          .id = ElementId{"cat_1"},
                                          .name = "Standards"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateTerm{.parent = ElementId{"termpkg_1"},
                                      .id = ElementId{"term_1"},
                                      .name = "ISO 26262",
                                      .value = "ISO 26262",
                                      .external_reference = "https://iso.org"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(CreateExpression{.parent = ElementId{"termpkg_1"},
                                            .id = ElementId{"expr_1"},
                                            .name = "Expr",
                                            .value = "{ISO 26262}"})
                    .applied);

    const auto* package =
        document.find_as<sacm::model::TerminologyPackage>(ElementId{"termpkg_1"});
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->terminology_elements().size(), 3u);
    EXPECT_TRUE(sacm::validation::validate(document).empty());

    // Round-trips through strict save.
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded =
        sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());

    // Deleting the term is policy-controlled and leaves the document valid.
    ASSERT_TRUE(document
                    .apply(DeleteElement{
                        .target = ElementId{"term_1"},
                        .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships})
                    .applied);
    EXPECT_EQ(document.find(ElementId{"term_1"}), nullptr);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

TEST(Sacm23Terminology, SACM23_TERM_001_RejectsTermOutsideTerminologyPackage) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"})
                    .applied);
    const auto result = document.apply(CreateTerm{
        .parent = ElementId{"acp_1"},
        .name = "term",
        .value = "term",
    });
    EXPECT_FALSE(result.applied);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().code, sacm::validation::codes::kCmdInvalidParent);
}

// Legacy Assurance Forge files write a TerminologyPackage's contents with the
// concrete class name as the element -- <expression id=.. value=..> -- instead
// of the canonical <terminologyElement xsi:type="sacm:Expression">. The library
// treated `expression` as a reference role, so those elements were dropped
// entirely and a whole terminology package vanished with no error (issue #201,
// found by the Phase 9 Stage 3 comparison). Tolerant mode must read them.
TEST(Sacm23Terminology, SACM23_TERM_001_LegacyTerminologyShorthandIsRead) {
    constexpr std::string_view kXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:version="2.0" xmi:id="acp_1">
  <name content="Case"/>
  <terminologyPackage id="TP1" name="Terminology">
    <expression id="TERM_SAFE" value="System operates without causing harm"/>
    <expression id="TERM_TEST" value="Verification through testing"/>
  </terminologyPackage>
</sacm:AssuranceCasePackage>)";

    const LoadResult loaded = sacm::io::load_xmi_string(kXml);
    ASSERT_TRUE(loaded.ok);

    // Both shorthand expressions are present as real Expression elements, with
    // their value carried through -- not dropped, not preserved as opaque blobs.
    const auto* safe = loaded.document->find_as<sacm::model::Expression>(ElementId{"TERM_SAFE"});
    ASSERT_NE(safe, nullptr) << "terminology shorthand <expression> was dropped";
    EXPECT_EQ(safe->value(), "System operates without causing harm");
    EXPECT_NE(loaded.document->find_as<sacm::model::Expression>(ElementId{"TERM_TEST"}), nullptr);

    // It re-exports as canonical SACM and round-trips.
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*loaded.document);
    ASSERT_TRUE(saved.ok);
    EXPECT_NE(saved.xml.find(R"(xsi:type="sacm:Expression")"), std::string::npos);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty());

    // Strict mode must still reject the shorthand -- it is a legacy form, not
    // SACM 2.3.
    const LoadResult strict = sacm::io::load_xmi_string(kXml, LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(strict.ok);
}

}  // namespace
