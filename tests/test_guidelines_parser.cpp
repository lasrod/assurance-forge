#include "parser/guidelines_parser.h"
#include "parser/sccg_dist_parser.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

const char* kMinimalGuidelinesYaml = R"yaml(
schema_version: "1.0.0"
sccg_version: "0.5.0"
document:
  title: "Safety Case Core Guidelines"
  license:
    id: "CC-BY-4.0"
source_policy:
  summary: "Paraphrased source guidance."
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
    statement: "State each claim as a sentence that can be shown true or false."
    rationale: "Reviewers need challengeable claims."
    review_prompts:
      - "Could a reviewer tell what would make this claim false?"
    examples:
      bad: "Brake monitor safety"
      problem: "This is a topic label."
      good: "Brake monitor response time meets the acceptance criterion."
    references:
      - source_id: "UL4600"
        clauses: ["5.2.3"]
    tool:
      applicable_elements: ["GSN Goal", "SACM Claim"]
      detection_hints:
        - "Look for topic labels."
      suggested_checks:
        - id: "check-claim-is-proposition"
          description: "Check whether claim text is a complete proposition."
review_profiles:
  - id: "claim_wording_review"
    display_name: "Claim wording review"
    description: "Reviews claim wording."
    applies_to: ["GSN Goal", "SACM Claim"]
    guideline_ids: ["CL.1"]
    required_data: ["SEL"]
    optional_data: ["PARENT"]
data_packages:
  - id: "SEL"
    display_name: "Selected element"
    description: "Selected element."
    required_fields: ["element_id", "element_type", "text"]
    optional_fields: []
prechecks:
  - id: "check-claim-is-proposition"
    display_name: "Claim is proposition"
    related_guideline_ids: ["CL.1"]
    expected_data: ["SEL"]
    result_type: "boolean_candidate"
    description: "Detects non-proposition claim text."
    interpretation: "Candidate finding only."
)yaml";

std::filesystem::path RepositoryGuidelinesPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "external" / "safety-case-core-guidelines" /
           "dist" / "sccg.full.yaml";
}

std::filesystem::path RepositorySccgDistPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "external" / "safety-case-core-guidelines" /
           "dist";
}

std::filesystem::path RepositorySccgSchemasPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "external" / "safety-case-core-guidelines" /
           "schemas";
}

} // namespace

TEST(GuidelinesParserTest, ParsesMinimalYamlAndFetchesGuidelines) {
    parser::GuidelinesParseResult result = parser::GuidelinesParser::ParseString(kMinimalGuidelinesYaml);

    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_EQ(result.document.schema_version, "1.0.0");
    EXPECT_EQ(result.document.sccg_version, "0.5.0");
    EXPECT_EQ(result.document.metadata.title, "Safety Case Core Guidelines");
    EXPECT_EQ(result.document.metadata.license.id, "CC-BY-4.0");
    ASSERT_EQ(result.document.metadata.recommendations.size(), 1);
    EXPECT_EQ(result.document.metadata.recommendations[0].method, "GSN-based development");

    const parser::Guideline* guideline = result.document.FindGuidelineById("CL.1");
    ASSERT_NE(guideline, nullptr);
    EXPECT_EQ(guideline->category, "CL");
    EXPECT_EQ(guideline->statement, "State each claim as a sentence that can be shown true or false.");
    EXPECT_EQ(guideline->rationale, "Reviewers need challengeable claims.");
    EXPECT_EQ(guideline->examples.good, "Brake monitor response time meets the acceptance criterion.");
    ASSERT_FALSE(guideline->tool.applicable_elements.empty());
    ASSERT_FALSE(guideline->references.empty());
    EXPECT_EQ(guideline->references[0].source_id, "UL4600");
    ASSERT_EQ(guideline->references[0].clauses.size(), 1);
    ASSERT_FALSE(guideline->references[0].clauses.empty());
    EXPECT_EQ(guideline->references[0].clauses[0], "5.2.3");

    std::vector<const parser::Guideline*> category_matches = result.document.FindGuidelinesByCategory("CL");
    ASSERT_EQ(category_matches.size(), 1);
    EXPECT_EQ(category_matches[0]->id, "CL.1");

    std::vector<const parser::Guideline*> element_matches =
        result.document.FindGuidelinesByApplicableElement("GSN Goal");
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

    const parser::ReviewProfile* profile = result.document.FindReviewProfileById("claim_wording_review");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->display_name, "Claim wording review");
    ASSERT_EQ(profile->guideline_ids.size(), 1u);
    EXPECT_EQ(profile->guideline_ids[0], "CL.1");

    std::vector<const parser::Guideline*> profile_matches =
        result.document.FindGuidelinesByReviewProfile("claim_wording_review");
    ASSERT_EQ(profile_matches.size(), 1u);
    EXPECT_EQ(profile_matches[0]->id, "CL.1");

    ASSERT_EQ(result.document.data_packages.size(), 1u);
    EXPECT_EQ(result.document.data_packages[0].id, "SEL");
    ASSERT_EQ(result.document.prechecks.size(), 1u);
    EXPECT_EQ(result.document.prechecks[0].related_guideline_ids[0], "CL.1");
}

TEST(GuidelinesParserTest, ParsesOldGuidelineFieldNamesAsFallback) {
    const char* yaml = R"yaml(
schema_version: "0.4.0"
guidelines:
  - id: "CL.1"
    category: "CL"
    title: "Old format"
    guideline: "Old statement."
    why: "Old rationale."
    example:
      good: "Old good example."
    tool_guidance:
      applicable_elements: ["GSN Goal"]
)yaml";

    parser::GuidelinesParseResult result = parser::GuidelinesParser::ParseString(yaml);

    ASSERT_TRUE(result.success) << result.error_message;
    const parser::Guideline* guideline = result.document.FindGuidelineById("CL.1");
    ASSERT_NE(guideline, nullptr);
    EXPECT_EQ(guideline->statement, "Old statement.");
    EXPECT_EQ(guideline->rationale, "Old rationale.");
    EXPECT_EQ(guideline->examples.good, "Old good example.");
    ASSERT_EQ(guideline->tool.applicable_elements.size(), 1u);
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
schema_version: "1.0.0"
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
    statement: "Text"
    rationale: "Reason"
    review_prompts: ["Prompt"]
    examples:
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
    EXPECT_EQ(result.document.schema_version, "1.0.0");
    EXPECT_EQ(result.document.sccg_version, "0.5.0");
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
    EXPECT_FALSE(claim_guideline->statement.empty());
    EXPECT_FALSE(claim_guideline->rationale.empty());
    EXPECT_FALSE(claim_guideline->tool.applicable_elements.empty());

    const parser::ReviewProfile* profile = result.document.FindReviewProfileById("claim_wording_review");
    ASSERT_NE(profile, nullptr);
    EXPECT_FALSE(profile->guideline_ids.empty());
    EXPECT_FALSE(result.document.FindGuidelinesByReviewProfile("claim_wording_review").empty());
    EXPECT_FALSE(result.document.data_packages.empty());
    EXPECT_FALSE(result.document.prechecks.empty());

    std::vector<const parser::Guideline*> goal_guidelines =
        result.document.FindGuidelinesByApplicableElement("GSN Goal");
    EXPECT_FALSE(goal_guidelines.empty());

    const parser::SuggestedCheck* proposition_check =
        result.document.FindSuggestedCheckById("check-claim-is-proposition");
    ASSERT_NE(proposition_check, nullptr);
    EXPECT_FALSE(proposition_check->description.empty());

    std::vector<const parser::Guideline*> proposition_guidelines =
        result.document.FindGuidelinesBySuggestedCheckId("check-claim-is-proposition");
    ASSERT_FALSE(proposition_guidelines.empty());
    EXPECT_EQ(proposition_guidelines.front()->id, "CL.1");
}

TEST(GuidelinesParserTest, ParsesRealSccgDistArtifacts) {
    parser::GuidelinesParseResult result = parser::SccgDistParser::ParseDirectory(RepositorySccgDistPath());

    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_EQ(result.document.schema_version, "1.0.0");
    EXPECT_EQ(result.document.sccg_version, "0.5.0");
    EXPECT_GT(result.document.guidelines.size(), 30u);
    EXPECT_FALSE(result.document.review_profiles.empty());
    EXPECT_FALSE(result.document.data_packages.empty());
    EXPECT_FALSE(result.document.prechecks.empty());

    const parser::Guideline* cl1 = result.document.FindGuidelineById("CL.1");
    ASSERT_NE(cl1, nullptr);
    EXPECT_EQ(cl1->rule_id, "CL.1");
    EXPECT_EQ(cl1->category_id, "CL");
    EXPECT_FALSE(cl1->rationale.empty());
    EXPECT_FALSE(cl1->review_profile_ids.empty());
    EXPECT_FALSE(cl1->data_package_ids.empty());

    const parser::ReviewProfile* profile = result.document.FindReviewProfileById("claim_wording_review");
    ASSERT_NE(profile, nullptr);
    EXPECT_FALSE(profile->applies_to.empty());
    EXPECT_FALSE(result.document.FindGuidelinesByReviewProfile(profile->id).empty());
}

TEST(GuidelinesParserTest, SccgSchemaContractsArePresentAndReadable) {
    const std::vector<std::string> schema_files = {
        "review_profiles.schema.json",
        "data_packages.schema.json",
        "ai_rule_export.schema.json",
        "prechecks.schema.json",
        "sccg.schema.json",
    };

    for (const std::string& schema_file : schema_files) {
        const std::filesystem::path path = RepositorySccgSchemasPath() / schema_file;
        ASSERT_TRUE(std::filesystem::exists(path)) << path.string();
        std::ifstream input(path);
        ASSERT_TRUE(input) << path.string();
        nlohmann::json schema;
        ASSERT_NO_THROW(input >> schema) << path.string();
        EXPECT_EQ(schema.value("$schema", ""), "http://json-schema.org/draft-07/schema#");
        EXPECT_TRUE(schema.contains("required")) << path.string();
    }
}