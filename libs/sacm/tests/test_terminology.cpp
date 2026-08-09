#include "sacm/compare/semantic_compare.h"
#include "sacm/io/xmi.h"
#include "sacm/model/document.h"
#include "sacm/validation/codes.h"
#include "sacm/validation/validate.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using sacm::commands::CreateAssuranceCasePackage;
using sacm::commands::CreateCategory;
using sacm::commands::CreateExpression;
using sacm::commands::CreateTerm;
using sacm::commands::CreateTerminologyPackage;
using sacm::commands::DeleteElement;
using sacm::commands::ReferenceDeletePolicy;
using sacm::commands::SetExpressionCategories;
using sacm::commands::SetExpressionValue;
using sacm::commands::SetTermExternalReference;
using sacm::commands::SetTermOrigin;
using sacm::io::LoadOptions;
using sacm::io::LoadResult;
using sacm::io::Mode;
using sacm::model::Document;
using sacm::model::ElementId;

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23" / name;
}

TEST(Sacm23Terminology, SACM23_TERM_001_TerminologyHeavyFixtureRoundTrips) {
    const LoadResult first =
        sacm::io::load_xmi_file(fixture("terminology-full-valid.sacm.xmi"), LoadOptions{.mode = Mode::Strict});
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
    ASSERT_EQ(category->categories().size(), 1u); // SACM 2.3 sub-category

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
    const LoadResult result = sacm::io::load_xmi_file(fixture("invalid/category-ref-wrong-type-invalid.sacm.xmi"),
                                                      LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value());
    const auto diagnostics = sacm::validation::validate(*result.document);
    EXPECT_TRUE(std::ranges::any_of(
        diagnostics, [](const auto& diagnostic) { return diagnostic.code == sacm::validation::codes::kRefWrongType; }));
}

TEST(Sacm23Terminology, SACM23_TERM_001_CreatesAndDeletesTerminologyElements) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"}).applied);
    ASSERT_TRUE(document
                    .apply(CreateTerminologyPackage{
                        .parent = ElementId{"acp_1"}, .id = ElementId{"termpkg_1"}, .name = "Vocab"})
                    .applied);
    ASSERT_TRUE(
        document.apply(CreateCategory{.parent = ElementId{"termpkg_1"}, .id = ElementId{"cat_1"}, .name = "Standards"})
            .applied);
    ASSERT_TRUE(document
                    .apply(CreateTerm{.parent = ElementId{"termpkg_1"},
                                      .id = ElementId{"term_1"},
                                      .name = "ISO 26262",
                                      .value = "ISO 26262",
                                      .external_reference = "https://iso.org"})
                    .applied);
    ASSERT_TRUE(
        document
            .apply(CreateExpression{
                .parent = ElementId{"termpkg_1"}, .id = ElementId{"expr_1"}, .name = "Expr", .value = "{ISO 26262}"})
            .applied);

    const auto* package = document.find_as<sacm::model::TerminologyPackage>(ElementId{"termpkg_1"});
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->terminology_elements().size(), 3u);
    EXPECT_TRUE(sacm::validation::validate(document).empty());

    // Round-trips through strict save.
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());

    // Deleting the term is policy-controlled and leaves the document valid.
    ASSERT_TRUE(document
                    .apply(DeleteElement{.target = ElementId{"term_1"},
                                         .reference_policy = ReferenceDeletePolicy::DeleteReferencingRelationships})
                    .applied);
    EXPECT_EQ(document.find(ElementId{"term_1"}), nullptr);
    EXPECT_TRUE(sacm::validation::validate(document).empty());
}

// A client must be able to edit a Term in place (value, external reference,
// origin, categories) -- the app's UpdateTerminologyTerm routes through these.
// Each command applies, and the edited term round-trips through strict save.
TEST(Sacm23Terminology, SACM23_TERM_001_UpdatesTermFieldsWithCommands) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"}).applied);
    ASSERT_TRUE(
        document.apply(CreateTerminologyPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"tp_1"}, .name = "Vocab"})
            .applied);
    ASSERT_TRUE(
        document.apply(CreateCategory{.parent = ElementId{"tp_1"}, .id = ElementId{"cat_1"}, .name = "C1"}).applied);
    ASSERT_TRUE(
        document.apply(CreateCategory{.parent = ElementId{"tp_1"}, .id = ElementId{"cat_2"}, .name = "C2"}).applied);
    ASSERT_TRUE(
        document.apply(CreateTerm{.parent = ElementId{"tp_1"}, .id = ElementId{"term_1"}, .name = "T", .value = "old"})
            .applied);

    ASSERT_TRUE(document.apply(SetExpressionValue{.element = ElementId{"term_1"}, .value = "new value"}).applied);
    ASSERT_TRUE(document
                    .apply(SetTermExternalReference{.element = ElementId{"term_1"},
                                                    .external_reference = "https://example.org"})
                    .applied);
    ASSERT_TRUE(document
                    .apply(SetExpressionCategories{.element = ElementId{"term_1"},
                                                   .categories = {ElementId{"cat_1"}, ElementId{"cat_2"}}})
                    .applied);
    // origin references any element (the library validates existence only).
    ASSERT_TRUE(document.apply(SetTermOrigin{.element = ElementId{"term_1"}, .origin = ElementId{"cat_1"}}).applied);

    const auto* term = document.find_as<sacm::model::Term>(ElementId{"term_1"});
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->value(), "new value");
    EXPECT_EQ(term->external_reference(), "https://example.org");
    ASSERT_EQ(term->categories().size(), 2u);
    ASSERT_TRUE(term->origin().has_value());
    EXPECT_EQ(term->origin()->value(), "cat_1");

    EXPECT_TRUE(sacm::validation::validate(document).empty());
    const auto saved = sacm::io::save_xmi_string(document);
    ASSERT_TRUE(saved.ok);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(document, *reloaded.document).empty());

    // origin can be cleared.
    ASSERT_TRUE(document.apply(SetTermOrigin{.element = ElementId{"term_1"}, .origin = std::nullopt}).applied);
    EXPECT_FALSE(document.find_as<sacm::model::Term>(ElementId{"term_1"})->origin().has_value());
}

// The term-update commands enforce their target kinds: value/categories require a
// Term or Expression, external reference/origin require a Term, and a category id
// must resolve to a Category.
TEST(Sacm23Terminology, SACM23_TERM_001_TermUpdateCommandsValidateTargets) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"}).applied);
    ASSERT_TRUE(
        document.apply(CreateTerminologyPackage{.parent = ElementId{"acp_1"}, .id = ElementId{"tp_1"}, .name = "Vocab"})
            .applied);
    ASSERT_TRUE(
        document.apply(CreateCategory{.parent = ElementId{"tp_1"}, .id = ElementId{"cat_1"}, .name = "C"}).applied);
    ASSERT_TRUE(
        document.apply(CreateTerm{.parent = ElementId{"tp_1"}, .id = ElementId{"term_1"}, .name = "T"}).applied);

    // value on a non-expression (a Category) is rejected.
    EXPECT_FALSE(document.apply(SetExpressionValue{.element = ElementId{"cat_1"}, .value = "x"}).applied);
    // external reference on a non-Term (a Category) is rejected.
    EXPECT_FALSE(
        document.apply(SetTermExternalReference{.element = ElementId{"cat_1"}, .external_reference = "x"}).applied);
    // a category id that is not a Category (the term itself) is rejected.
    const auto bad_category =
        document.apply(SetExpressionCategories{.element = ElementId{"term_1"}, .categories = {ElementId{"term_1"}}});
    EXPECT_FALSE(bad_category.applied);
    EXPECT_TRUE(std::ranges::any_of(bad_category.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kRefWrongType;
    }));
}

TEST(Sacm23Terminology, SACM23_TERM_001_RejectsTermOutsideTerminologyPackage) {
    Document document;
    ASSERT_TRUE(document.apply(CreateAssuranceCasePackage{.id = ElementId{"acp_1"}, .name = "A"}).applied);
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
// concrete class name as the element -- <expression id=.. value=..>, and
// likewise <term> and <category> -- instead of the canonical
// <terminologyElement xsi:type="sacm:Expression">. The library treated these as
// reference roles, so the elements were dropped entirely and a whole terminology
// package vanished with no error (issue #201, found by the Phase 9 Stage 3
// comparison). Tolerant mode must read all three concrete forms.
TEST(Sacm23Terminology, SACM23_TERM_001_LegacyTerminologyShorthandIsRead) {
    constexpr std::string_view kXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:version="2.0" xmi:id="acp_1">
  <name content="Case"/>
  <terminologyPackage id="TP1" name="Terminology">
    <category id="CAT_STANDARDS" name="Standards"/>
    <term id="TERM_ISO" value="ISO 26262" category="CAT_STANDARDS"/>
    <expression id="TERM_SAFE" value="System operates without causing harm"/>
  </terminologyPackage>
</sacm:AssuranceCasePackage>)";

    const LoadResult loaded = sacm::io::load_xmi_string(kXml);
    ASSERT_TRUE(loaded.ok);

    // All three shorthand kinds resolve to their real element types with values
    // carried through -- not dropped, not preserved as opaque blobs.
    const auto* expression = loaded.document->find_as<sacm::model::Expression>(ElementId{"TERM_SAFE"});
    ASSERT_NE(expression, nullptr) << "shorthand <expression> was dropped";
    EXPECT_EQ(expression->value(), "System operates without causing harm");

    const auto* term = loaded.document->find_as<sacm::model::Term>(ElementId{"TERM_ISO"});
    ASSERT_NE(term, nullptr) << "shorthand <term> was dropped";
    EXPECT_EQ(term->value(), "ISO 26262");
    ASSERT_EQ(term->categories().size(), 1u);
    EXPECT_EQ(term->categories().front().value(), "CAT_STANDARDS");

    const auto* category = loaded.document->find_as<sacm::model::Category>(ElementId{"CAT_STANDARDS"});
    ASSERT_NE(category, nullptr) << "shorthand <category> was dropped";

    // All three are contained in the package, not scattered elsewhere.
    const auto* package = loaded.document->find_as<sacm::model::TerminologyPackage>(ElementId{"TP1"});
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->terminology_elements().size(), 3u);

    // It re-exports as canonical SACM and round-trips.
    const sacm::io::SaveResult saved = sacm::io::save_xmi_string(*loaded.document);
    ASSERT_TRUE(saved.ok);
    EXPECT_NE(saved.xml.find(R"(xsi:type="sacm:Expression")"), std::string::npos);
    EXPECT_NE(saved.xml.find(R"(xsi:type="sacm:Term")"), std::string::npos);
    EXPECT_NE(saved.xml.find(R"(xsi:type="sacm:Category")"), std::string::npos);
    const LoadResult reloaded = sacm::io::load_xmi_string(saved.xml);
    ASSERT_TRUE(reloaded.ok);
    EXPECT_TRUE(sacm::compare::semantic_compare(*loaded.document, *reloaded.document).empty());

    // Strict mode must still reject the shorthand -- it is a legacy form, not
    // SACM 2.3.
    const LoadResult strict = sacm::io::load_xmi_string(kXml, LoadOptions{.mode = Mode::Strict});
    EXPECT_FALSE(strict.ok);
}

// A terminology package whose two ExpressionElements the caller attributes, so
// each clause-10.10 case differs only in isAbstract.
std::vector<sacm::validation::Diagnostic> validate_terminology(std::string_view term_attrs,
                                                               std::string_view expression_attrs) {
    const std::string xml =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)"
                    R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
                    R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
                    R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
                    R"(<terminologyPackage xmi:id="tp_1"><name content="Vocabulary"/>)"
                    R"(<terminologyElement xsi:type="sacm:Term" xmi:id="term_1" value="hazard" )") +
        std::string(term_attrs) + R"(><name content="Hazard"/></terminologyElement>)" +
        R"(<terminologyElement xsi:type="sacm:Expression" xmi:id="expr_1" value="{hazard}" element="term_1" )" +
        std::string(expression_attrs) + R"(><name content="Phrase"/></terminologyElement>)" +
        R"(</terminologyPackage></sacm:AssuranceCasePackage>)";
    const LoadResult result = sacm::io::load_xmi_string(xml, LoadOptions{.mode = Mode::Strict});
    EXPECT_TRUE(result.document.has_value()) << (result.diagnostics.empty() ? "" : result.diagnostics.front().message);
    if (!result.document.has_value()) {
        return {};
    }
    return sacm::validation::validate(*result.document);
}

// Clause 10.10 OCL:
// self.isAbstract = false implies self.element->forall(expr|expr.isAbstract = false)
TEST(Sacm23Terminology, SACM23_TERM_001_ConcreteExpressionCannotReferenceAbstractElements) {
    const auto abstract_part = validate_terminology(R"(isAbstract="true")", "");
    EXPECT_TRUE(std::ranges::any_of(abstract_part, [](const sacm::validation::Diagnostic& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kAbstractnessInvalid;
    })) << (abstract_part.empty() ? "no diagnostics at all" : abstract_part.front().message);

    // Both concrete: clean.
    EXPECT_TRUE(validate_terminology("", "").empty());
    // An abstract expression may reference abstract parts -- the OCL's
    // antecedent is `isAbstract = false`.
    EXPECT_TRUE(validate_terminology(R"(isAbstract="true")", R"(isAbstract="true")").empty());
}

// Clause 8.4: "If expression is not empty, then +content should be empty." A
// "should", so a warning -- but a real one: the two carry the same meaning
// twice and a reader has no rule for which wins.
TEST(Sacm23Terminology, SACM23_BASE_001_ExpressionLangStringWithLiteralContentIsDiagnosed) {
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
        R"(<terminologyPackage xmi:id="tp_1"><name content="Vocabulary"/>)"
        R"(<terminologyElement xsi:type="sacm:Term" xmi:id="term_1" value="hazard"><name content="Hazard"/>)"
        R"(</terminologyElement></terminologyPackage>)"
        R"(<argumentPackage xmi:id="ap_1"><name content="Args"/>)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="claim_1"><name content="C"/>)"
        R"(<description xmi:id="d_1"><content>)"
        R"(<value xsi:type="sacm:ExpressionLangString" lang="en" expression="term_1" content="hazard"/>)"
        R"(</content></description></argumentElement></argumentPackage></sacm:AssuranceCasePackage>)";
    const LoadResult result = sacm::io::load_xmi_string(xml, LoadOptions{.mode = Mode::Strict});
    ASSERT_TRUE(result.document.has_value()) << (result.diagnostics.empty() ? "" : result.diagnostics.front().message);
    const auto diagnostics = sacm::validation::validate(*result.document);
    ASSERT_TRUE(std::ranges::any_of(diagnostics, [](const sacm::validation::Diagnostic& diagnostic) {
        return diagnostic.code == sacm::validation::codes::kExpressionContentConflict;
    })) << (diagnostics.empty() ? "no diagnostics at all" : diagnostics.front().message);
    EXPECT_TRUE(std::ranges::all_of(diagnostics, [](const sacm::validation::Diagnostic& diagnostic) {
        return diagnostic.severity == sacm::validation::Severity::Warning;
    }));
}

} // namespace
