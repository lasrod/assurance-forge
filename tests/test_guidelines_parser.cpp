#include "parser/guidelines_parser.h"
#include "parser/sccg_dist_parser.h"

#include <algorithm>
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
    auto result = parser::GuidelinesParser::ParseString(kMinimalGuidelinesYaml);

    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error());
    EXPECT_EQ(result.value().schema_version, "1.0.0");
    EXPECT_EQ(result.value().sccg_version, "0.5.0");
    EXPECT_EQ(result.value().metadata.title, "Safety Case Core Guidelines");
    EXPECT_EQ(result.value().metadata.license.id, "CC-BY-4.0");
    ASSERT_EQ(result.value().metadata.recommendations.size(), 1);
    EXPECT_EQ(result.value().metadata.recommendations[0].method, "GSN-based development");

    const parser::Guideline* guideline = result.value().FindGuidelineById("CL.1");
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

    std::vector<const parser::Guideline*> category_matches = result.value().FindGuidelinesByCategory("CL");
    ASSERT_EQ(category_matches.size(), 1);
    EXPECT_EQ(category_matches[0]->id, "CL.1");

    std::vector<const parser::Guideline*> element_matches =
        result.value().FindGuidelinesByApplicableElement("GSN Goal");
    ASSERT_EQ(element_matches.size(), 1);
    EXPECT_EQ(element_matches[0]->id, "CL.1");

    const parser::SuggestedCheck* check = result.value().FindSuggestedCheckById("check-claim-is-proposition");
    ASSERT_NE(check, nullptr);
    EXPECT_EQ(check->description, "Check whether claim text is a complete proposition.");

    std::vector<const parser::Guideline*> check_matches =
        result.value().FindGuidelinesBySuggestedCheckId("check-claim-is-proposition");
    ASSERT_EQ(check_matches.size(), 1);
    EXPECT_EQ(check_matches[0]->id, "CL.1");

    const parser::ReferenceSource* source = result.value().FindReferenceSourceById("UL4600");
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->display_name, "UL 4600");

    const parser::GuidelineCategory* category = result.value().FindCategoryById("CL");
    ASSERT_NE(category, nullptr);
    EXPECT_EQ(category->title, "Claim rules");

    const parser::ReviewProfile* profile = result.value().FindReviewProfileById("claim_wording_review");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->display_name, "Claim wording review");
    ASSERT_EQ(profile->guideline_ids.size(), 1u);
    EXPECT_EQ(profile->guideline_ids[0], "CL.1");

    std::vector<const parser::Guideline*> profile_matches =
        result.value().FindGuidelinesByReviewProfile("claim_wording_review");
    ASSERT_EQ(profile_matches.size(), 1u);
    EXPECT_EQ(profile_matches[0]->id, "CL.1");

    ASSERT_EQ(result.value().data_packages.size(), 1u);
    EXPECT_EQ(result.value().data_packages[0].id, "SEL");
    ASSERT_EQ(result.value().prechecks.size(), 1u);
    EXPECT_EQ(result.value().prechecks[0].related_guideline_ids[0], "CL.1");
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

    auto result = parser::GuidelinesParser::ParseString(yaml);

    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error());
    const parser::Guideline* guideline = result.value().FindGuidelineById("CL.1");
    ASSERT_NE(guideline, nullptr);
    EXPECT_EQ(guideline->statement, "Old statement.");
    EXPECT_EQ(guideline->rationale, "Old rationale.");
    EXPECT_EQ(guideline->examples.good, "Old good example.");
    ASSERT_EQ(guideline->tool.applicable_elements.size(), 1u);
}

TEST(GuidelinesParserTest, ReportsInvalidYaml) {
    auto result = parser::GuidelinesParser::ParseString("schema_version: [");

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

TEST(GuidelinesParserTest, ReportsMissingFile) {
    auto result = parser::GuidelinesParser::ParseFile("missing_guidelines_file_12345.yaml");

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
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

    auto result = parser::GuidelinesParser::ParseString(yaml);

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("missing id"), std::string::npos);
}

TEST(GuidelinesParserTest, ParsesRealGuidelinesFile) {
    auto result = parser::GuidelinesParser::ParseFile(RepositoryGuidelinesPath().string());

    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error());
    EXPECT_EQ(result.value().schema_version, "1.0.0");
    EXPECT_EQ(result.value().sccg_version, "0.6.0");
    EXPECT_EQ(result.value().metadata.title, "Safety Case Core Guidelines");
    EXPECT_FALSE(result.value().categories.empty());
    EXPECT_FALSE(result.value().reference_sources.empty());
    EXPECT_GT(result.value().guidelines.size(), 30);

    const parser::GuidelineCategory* claim_category = result.value().FindCategoryById("CL");
    ASSERT_NE(claim_category, nullptr);
    EXPECT_EQ(claim_category->index_title, "CL. Claim guidance");

    const parser::Guideline* claim_guideline = result.value().FindGuidelineById("CL.1");
    ASSERT_NE(claim_guideline, nullptr);
    EXPECT_EQ(claim_guideline->category, "CL");
    EXPECT_EQ(claim_guideline->title, "Write each claim as a falsifiable proposition");
    EXPECT_FALSE(claim_guideline->statement.empty());
    EXPECT_FALSE(claim_guideline->rationale.empty());
    EXPECT_FALSE(claim_guideline->tool.applicable_elements.empty());

    const parser::ReviewProfile* profile = result.value().FindReviewProfileById("claim_review");
    ASSERT_NE(profile, nullptr);
    EXPECT_FALSE(profile->guideline_ids.empty());
    EXPECT_FALSE(result.value().FindGuidelinesByReviewProfile("claim_review").empty());
    EXPECT_FALSE(result.value().data_packages.empty());
    EXPECT_FALSE(result.value().prechecks.empty());

    std::vector<const parser::Guideline*> goal_guidelines =
        result.value().FindGuidelinesByApplicableElement("GSN Goal");
    EXPECT_FALSE(goal_guidelines.empty());

    const parser::SuggestedCheck* proposition_check =
        result.value().FindSuggestedCheckById("check-claim-is-proposition");
    ASSERT_NE(proposition_check, nullptr);
    EXPECT_FALSE(proposition_check->description.empty());

    std::vector<const parser::Guideline*> proposition_guidelines =
        result.value().FindGuidelinesBySuggestedCheckId("check-claim-is-proposition");
    ASSERT_FALSE(proposition_guidelines.empty());
    EXPECT_EQ(proposition_guidelines.front()->id, "CL.1");
}

TEST(GuidelinesParserTest, ParsesRealSccgDistArtifacts) {
    auto result = parser::SccgDistParser::ParseDirectory(RepositorySccgDistPath());

    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error());
    EXPECT_EQ(result.value().schema_version, "1.0.0");
    EXPECT_EQ(result.value().sccg_version, "0.6.0");
    EXPECT_GT(result.value().guidelines.size(), 30u);
    EXPECT_FALSE(result.value().review_profiles.empty());
    EXPECT_FALSE(result.value().data_packages.empty());
    EXPECT_FALSE(result.value().prechecks.empty());

    const parser::Guideline* cl1 = result.value().FindGuidelineById("CL.1");
    ASSERT_NE(cl1, nullptr);
    EXPECT_EQ(cl1->rule_id, "CL.1");
    EXPECT_EQ(cl1->category_id, "CL");
    EXPECT_FALSE(cl1->rationale.empty());
    EXPECT_FALSE(cl1->review_profile_ids.empty());
    EXPECT_FALSE(cl1->data_package_ids.empty());

    const parser::ReviewProfile* profile = result.value().FindReviewProfileById("claim_review");
    ASSERT_NE(profile, nullptr);
    EXPECT_FALSE(profile->applies_to.empty());
    EXPECT_FALSE(result.value().FindGuidelinesByReviewProfile(profile->id).empty());

    const parser::ReviewProfile* strategy_profile = result.value().FindReviewProfileById("strategy_review");
    ASSERT_NE(strategy_profile, nullptr);
    for (const std::string applicable_element : {"GSN Strategy", "CAE Argument"}) {
        const bool applies =
            std::find(strategy_profile->applies_to.begin(), strategy_profile->applies_to.end(), applicable_element) !=
            strategy_profile->applies_to.end();
        EXPECT_TRUE(applies) << applicable_element;
    }
    EXPECT_EQ(result.value().review_profiles.size(), 7u);
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
