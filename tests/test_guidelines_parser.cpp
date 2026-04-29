#include <gtest/gtest.h>

#include "parser/guidelines_parser.h"

#include <filesystem>

namespace {

const char* kMinimalGuidelinesYaml = R"yaml(
schema_version: "0.4.0"
document:
  title: "Safety Case Core Guidelines"
  license:
    id: "CC-BY-4.0"
  method_application_guidance:
    recommendations:
      - method: "GSN-based development"
        recommendation: "Apply GSN mappings."
  id_scheme:
    - prefix: "CL"
      meaning: "Claim guidance"
  required_guideline_sections:
    - "Guideline"
reference_sources:
  - id: "UL4600"
    display_name: "UL 4600"
    type: "standard"
categories:
  - id: "CL"
    title: "Claim rules"
    index_title: "Claim guidance"
guidelines:
  - id: "CL.1"
    category: "CL"
    title: "Write each claim as a falsifiable proposition"
    guideline: "State each claim as a sentence that can be shown true or false."
    why: "Reviewers need challengeable claims."
    review_prompts:
      - "Could a reviewer tell what would make this claim false?"
    example:
      bad: "Brake monitor safety"
      problem: "This is a topic label."
      good: "Brake monitor response time meets the acceptance criterion."
    references:
      - source_id: "UL4600"
        clauses: ["5.2.3"]
    tool_guidance:
      applicable_elements: ["GSN Goal", "SACM Claim"]
      detection_hints:
        - "Look for topic labels."
      suggested_checks:
        - id: "check-claim-is-proposition"
          description: "Check whether claim text is a complete proposition."
)yaml";

std::filesystem::path RepositoryGuidelinesPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "external" / "safety-case-core-guidelines" / "data" / "guidelines.yaml";
}

}  // namespace

TEST(GuidelinesParserTest, ParsesMinimalYamlAndFetchesGuidelines) {
    parser::GuidelinesParseResult result = parser::GuidelinesParser::ParseString(kMinimalGuidelinesYaml);

    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_EQ(result.document.schema_version, "0.4.0");
    EXPECT_EQ(result.document.metadata.title, "Safety Case Core Guidelines");
    EXPECT_EQ(result.document.metadata.license.id, "CC-BY-4.0");
    ASSERT_EQ(result.document.metadata.recommendations.size(), 1);
    EXPECT_EQ(result.document.metadata.recommendations[0].method, "GSN-based development");

    const parser::Guideline* guideline = result.document.FindGuidelineById("CL.1");
    ASSERT_NE(guideline, nullptr);
    EXPECT_EQ(guideline->category, "CL");
    ASSERT_FALSE(guideline->references.empty());
    EXPECT_EQ(guideline->references[0].source_id, "UL4600");
    ASSERT_EQ(guideline->references[0].clauses.size(), 1);
    ASSERT_FALSE(guideline->references[0].clauses.empty());
    EXPECT_EQ(guideline->references[0].clauses[0], "5.2.3");

    std::vector<const parser::Guideline*> category_matches = result.document.FindGuidelinesByCategory("CL");
    ASSERT_EQ(category_matches.size(), 1);
    EXPECT_EQ(category_matches[0]->id, "CL.1");

    std::vector<const parser::Guideline*> element_matches = result.document.FindGuidelinesByApplicableElement("GSN Goal");
    ASSERT_EQ(element_matches.size(), 1);
    EXPECT_EQ(element_matches[0]->id, "CL.1");

    const parser::SuggestedCheck* check = result.document.FindSuggestedCheckById("check-claim-is-proposition");
    ASSERT_NE(check, nullptr);
    EXPECT_EQ(check->description, "Check whether claim text is a complete proposition.");

    std::vector<const parser::Guideline*> check_matches =
      result.document.FindGuidelinesBySuggestedCheckId("check-claim-is-proposition");
    ASSERT_EQ(check_matches.size(), 1);
    EXPECT_EQ(check_matches[0]->id, "CL.1");

    const parser::ReferenceSource* source = result.document.FindReferenceSourceById("UL4600");
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->display_name, "UL 4600");

    const parser::GuidelineCategory* category = result.document.FindCategoryById("CL");
    ASSERT_NE(category, nullptr);
    EXPECT_EQ(category->title, "Claim rules");
}

TEST(GuidelinesParserTest, ReportsInvalidYaml) {
    parser::GuidelinesParseResult result = parser::GuidelinesParser::ParseString("schema_version: [");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST(GuidelinesParserTest, ReportsMissingFile) {
    parser::GuidelinesParseResult result = parser::GuidelinesParser::ParseFile("missing_guidelines_file_12345.yaml");

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST(GuidelinesParserTest, RejectsMissingGuidelineIdentity) {
    const char* yaml = R"yaml(
schema_version: "0.4.0"
document:
  title: "Safety Case Core Guidelines"
reference_sources:
  - id: "UL4600"
    display_name: "UL 4600"
categories:
  - id: "CL"
    title: "Claim rules"
guidelines:
  - category: "CL"
    title: "Missing id"
    guideline: "Text"
    why: "Reason"
    review_prompts: ["Prompt"]
    example:
      bad: "Bad"
      problem: "Problem"
      good: "Good"
    references:
      - source_id: "UL4600"
)yaml";

    parser::GuidelinesParseResult result = parser::GuidelinesParser::ParseString(yaml);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("missing id"), std::string::npos);
}

TEST(GuidelinesParserTest, ParsesRealGuidelinesFile) {
    parser::GuidelinesParseResult result = parser::GuidelinesParser::ParseFile(RepositoryGuidelinesPath().string());

    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_EQ(result.document.schema_version, "0.4.0");
    EXPECT_EQ(result.document.metadata.title, "Safety Case Core Guidelines");
    EXPECT_FALSE(result.document.categories.empty());
    EXPECT_FALSE(result.document.reference_sources.empty());
    EXPECT_GT(result.document.guidelines.size(), 30);

    const parser::GuidelineCategory* claim_category = result.document.FindCategoryById("CL");
    ASSERT_NE(claim_category, nullptr);
    EXPECT_EQ(claim_category->index_title, "CL. Claim guidance");

    const parser::Guideline* claim_guideline = result.document.FindGuidelineById("CL.1");
    ASSERT_NE(claim_guideline, nullptr);
    EXPECT_EQ(claim_guideline->category, "CL");
    EXPECT_EQ(claim_guideline->title, "Write each claim as a falsifiable proposition");
    EXPECT_FALSE(claim_guideline->tool_guidance.applicable_elements.empty());

    std::vector<const parser::Guideline*> goal_guidelines = result.document.FindGuidelinesByApplicableElement("GSN Goal");
    EXPECT_FALSE(goal_guidelines.empty());

    const parser::SuggestedCheck* proposition_check = result.document.FindSuggestedCheckById("check-claim-is-proposition");
    ASSERT_NE(proposition_check, nullptr);
    EXPECT_FALSE(proposition_check->description.empty());

    std::vector<const parser::Guideline*> proposition_guidelines =
      result.document.FindGuidelinesBySuggestedCheckId("check-claim-is-proposition");
    ASSERT_FALSE(proposition_guidelines.empty());
    EXPECT_EQ(proposition_guidelines.front()->id, "CL.1");
}