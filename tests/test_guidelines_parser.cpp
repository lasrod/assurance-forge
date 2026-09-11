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
    applies_to: ["GSN Goal", "CAE Claim"]
    guideline_ids: ["CL.1"]
    required_data: ["SELECTED_CLAIM"]
    optional_data: ["PARENT"]
    when_absent:
      - id: "PARENT"
        statement: "Without the parent, judge wording alone."
        unassessable_guideline_ids: ["CL.1"]
selectable_elements:
  - element: "GSN Goal"
    notation: "GSN"
    element_role: "claim"
    basis: "GSN Community Standard v3 core element (Goal)."
data_packages:
  - id: "SELECTED_CLAIM"
    display_name: "Selected claim"
    description: "Selected claim."
    role: "selected_element"
    element_role: "claim"
    required_fields: ["element_id", "element_type", "text"]
    optional_fields: []
availability_states:
  - id: "withheld"
    display_name: "Withheld"
    meaning: "The data exists and was deliberately not shared."
prechecks:
  - id: "check-claim-is-proposition"
    display_name: "Claim is proposition"
    related_guideline_ids: ["CL.1"]
    expected_data: ["SELECTED_CLAIM"]
    result_type: "boolean_candidate"
    description: "Detects non-proposition claim text."
    fires_when: "The claim has no predicate asserting a property."
    interpretation: "Candidate finding only."
authoring_guidance:
  description: "The writing-time subset."
  usage: "Render each rule from short_rule and cite its id."
  core_rules:
    - id: "CL.1"
      category: "CL"
      short_rule: "State each claim as a proposition."
      statement: "State each claim as a sentence that can be shown true or false."
      reason: "A topic label cannot be reviewed."
  element_rules:
    - element_role: "claim"
      elements: ["GSN Goal", "CAE Claim"]
      guideline_ids: ["CL.1"]
      review_profile_id: "claim_wording_review"
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
    EXPECT_EQ(result.value().data_packages[0].id, "SELECTED_CLAIM");
    EXPECT_EQ(result.value().data_packages[0].role, "selected_element");
    EXPECT_EQ(result.value().data_packages[0].element_role, "claim");
    ASSERT_EQ(result.value().prechecks.size(), 1u);
    EXPECT_EQ(result.value().prechecks[0].related_guideline_ids[0], "CL.1");
    EXPECT_FALSE(result.value().prechecks[0].fires_when.empty());

    // The lookup the review method uses in place of naming a package: the
    // profile requires one package whose role is `selected_element`, and that
    // package carries the element role the profile reviews.
    const parser::DataPackage* selected = result.value().FindSelectedElementPackage(*profile);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->id, "SELECTED_CLAIM");
    EXPECT_EQ(result.value().ElementRoleForSelectableElement("GSN Goal"), "claim");
    EXPECT_TRUE(result.value().ElementRoleForSelectableElement("GSN Nonesuch").empty());
    ASSERT_EQ(profile->when_absent.size(), 1u);
    EXPECT_EQ(profile->when_absent[0].id, "PARENT");
    ASSERT_NE(result.value().FindAvailabilityStateById("withheld"), nullptr);

    const parser::AuthoringElementRule* element_rule = result.value().FindAuthoringElementRule("claim");
    ASSERT_NE(element_rule, nullptr);
    EXPECT_EQ(element_rule->review_profile_id, "claim_wording_review");
    ASSERT_EQ(result.value().authoring_guidance.core_rules.size(), 1u);
    EXPECT_EQ(result.value().authoring_guidance.core_rules[0].short_rule, "State each claim as a proposition.");
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
    EXPECT_EQ(result.value().schema_version, "3.0.0");
    EXPECT_EQ(result.value().sccg_version, "0.8.0");
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
    EXPECT_EQ(result.value().schema_version, "3.0.0");
    EXPECT_EQ(result.value().sccg_version, "0.8.0");
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

    // Every released profile names exactly one selected-element package, and its
    // role is one the catalog publishes as selectable. The dist parser refuses a
    // catalog where that is not true, so this is the positive half of that gate.
    for (const parser::ReviewProfile& released_profile : result.value().review_profiles) {
        SCOPED_TRACE(released_profile.id);
        const parser::DataPackage* selected = result.value().FindSelectedElementPackage(released_profile);
        ASSERT_NE(selected, nullptr);
        EXPECT_FALSE(selected->element_role.empty());
        bool role_is_selectable = false;
        for (const parser::SelectableElement& element : result.value().selectable_elements) {
            role_is_selectable = role_is_selectable || element.element_role == selected->element_role;
        }
        EXPECT_TRUE(role_is_selectable) << selected->element_role;
    }

    // The tool contract 0.7.0 publishes, read off the same files the runtime
    // loads: word lists with an effect, numeric thresholds, notation-neutral
    // repairs, the four availability states, the writing-time subset, and the
    // document block that used to reach only the YAML fallback.
    const parser::Guideline* cl5 = result.value().FindGuidelineById("CL.5");
    ASSERT_NE(cl5, nullptr);
    EXPECT_FALSE(cl5->short_rule.empty());
    EXPECT_FALSE(cl5->tool.markers.empty());
    EXPECT_FALSE(cl5->tool.repair.empty());
    bool cl5_suppresses = false;
    for (const parser::GuidelineMarker& marker : cl5->tool.markers) {
        EXPECT_FALSE(marker.effect.empty());
        cl5_suppresses = cl5_suppresses || marker.effect == "suppress";
    }
    EXPECT_TRUE(cl5_suppresses);

    const parser::Guideline* cl3 = result.value().FindGuidelineById("CL.3");
    ASSERT_NE(cl3, nullptr);
    ASSERT_FALSE(cl3->tool.thresholds.empty());
    EXPECT_EQ(cl3->tool.thresholds[0].id, "claim_word_count");
    EXPECT_GT(cl3->tool.thresholds[0].value, 0.0);

    const parser::Guideline* ar2 = result.value().FindGuidelineById("AR.2");
    ASSERT_NE(ar2, nullptr);
    ASSERT_FALSE(ar2->tool.repair.empty());
    EXPECT_EQ(ar2->tool.repair[0].action, "add_element");
    EXPECT_EQ(ar2->tool.repair[0].element_role, "strategy");
    EXPECT_EQ(ar2->tool.repair[0].attach_to, "between_selected_and_children");

    for (const char* state_id : {"available", "not_implemented", "empty", "withheld"}) {
        EXPECT_NE(result.value().FindAvailabilityStateById(state_id), nullptr) << state_id;
    }

    // SCCG 0.8.0 removed the statement that silenced EV.5 and seven other
    // guidelines when EVIDENCE_BASIS was absent (safety-case-core-guidelines#13):
    // the package is optional for evidence review, and no profile publishes a
    // when_absent entry any more -- the registry-wide rule governs every absence.
    const parser::ReviewProfile* evidence_profile = result.value().FindReviewProfileById("evidence_review");
    ASSERT_NE(evidence_profile, nullptr);
    EXPECT_TRUE(evidence_profile->when_absent.empty());
    EXPECT_NE(
        std::find(evidence_profile->optional_data.begin(), evidence_profile->optional_data.end(), "EVIDENCE_BASIS"),
        evidence_profile->optional_data.end());

    EXPECT_EQ(result.value().metadata.title, "Safety Case Core Guidelines");
    EXPECT_FALSE(result.value().metadata.purpose.empty());
    EXPECT_EQ(result.value().metadata.license.id, "CC-BY-4.0");

    EXPECT_FALSE(result.value().authoring_guidance.core_rules.empty());
    EXPECT_FALSE(result.value().authoring_guidance.usage.empty());
    const parser::AuthoringElementRule* claim_rule = result.value().FindAuthoringElementRule("claim");
    ASSERT_NE(claim_rule, nullptr);
    EXPECT_EQ(claim_rule->review_profile_id, "claim_review");

    // Every prechecks entry states its firing condition, so two tools implement
    // the same check rather than each reading the description for itself.
    for (const parser::Precheck& precheck : result.value().prechecks) {
        EXPECT_FALSE(precheck.fires_when.empty()) << precheck.id;
    }

    // Two routes from an element role to the profile that judges it: the
    // published `element_rules`, and the profile whose required
    // selected-element package carries that role. The second is what the MCP
    // prompts fall back to when `authoring_guidance.json` is absent -- it is
    // optional in a dist directory -- so the two must agree, or the fallback
    // would quietly quote a different profile's guidelines.
    ASSERT_FALSE(result.value().authoring_guidance.element_rules.empty());
    for (const parser::AuthoringElementRule& rule : result.value().authoring_guidance.element_rules) {
        SCOPED_TRACE(rule.element_role);
        const parser::ReviewProfile* derived = result.value().FindReviewProfileForElementRole(rule.element_role);
        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(derived->id, rule.review_profile_id);
    }
    EXPECT_EQ(result.value().FindReviewProfileForElementRole("nonesuch"), nullptr);
    EXPECT_EQ(result.value().FindReviewProfileForElementRole(""), nullptr);
}

// SCCG 0.8.0 adds four things a tool has to read, and both loaders must read
// them the same way: a catalogue read one way by the dist path every build takes
// and another by the YAML fallback would review differently depending on which
// file happened to be found.
TEST(GuidelinesParserTest, BothLoadersReadTheSccg080Contract) {
    auto dist = parser::SccgDistParser::ParseDirectory(RepositorySccgDistPath());
    auto yaml = parser::GuidelinesParser::ParseFile(RepositoryGuidelinesPath().string());
    ASSERT_TRUE(dist.has_value()) << (dist ? "" : dist.error());
    ASSERT_TRUE(yaml.has_value()) << (yaml ? "" : yaml.error());

    for (const parser::GuidelinesDocument* document : {&dist.value(), &yaml.value()}) {
        SCOPED_TRACE(document == &dist.value() ? "dist" : "yaml");

        // The rule for every package a review was not given.
        EXPECT_NE(document->when_unavailable.find("never silences a guideline"), std::string::npos)
            << document->when_unavailable;

        // Three guidelines retired, each redirecting to the ones that carry it.
        // A retired id is no longer a guideline, and is findable only as retired.
        for (const char* retired_id : {"AR.3", "SU.9", "RD.6"}) {
            SCOPED_TRACE(retired_id);
            EXPECT_EQ(document->FindGuidelineById(retired_id), nullptr);
            const parser::RetiredGuideline* retired = document->FindRetiredGuidelineById(retired_id);
            ASSERT_NE(retired, nullptr);
            EXPECT_EQ(retired->retired_in, "0.8.0");
            EXPECT_FALSE(retired->replaced_by.empty());
            for (const std::string& replacement : retired->replaced_by)
                EXPECT_NE(document->FindGuidelineById(replacement), nullptr) << replacement;
        }
        const parser::RetiredGuideline* ar3 = document->FindRetiredGuidelineById("AR.3");
        ASSERT_NE(ar3, nullptr);
        EXPECT_EQ(ar3->replaced_by, (std::vector<std::string>{"AR.7", "AR.6"}));

        // The disambiguation published for pairs observed to be confused: CL.4
        // with CL.5 and with AR.4, the reciprocal swap the probe corpus found.
        const parser::Guideline* cl4 = document->FindGuidelineById("CL.4");
        ASSERT_NE(cl4, nullptr);
        std::vector<std::string> neighbours;
        for (const parser::GuidelineDistinction& distinction : cl4->distinguish_from) {
            EXPECT_FALSE(distinction.note.empty()) << distinction.id;
            neighbours.push_back(distinction.id);
        }
        EXPECT_NE(std::find(neighbours.begin(), neighbours.end(), "CL.5"), neighbours.end());
        EXPECT_NE(std::find(neighbours.begin(), neighbours.end(), "AR.4"), neighbours.end());

        // claim_review in four passes that partition it exactly.
        const parser::ReviewProfile* claim = document->FindReviewProfileById("claim_review");
        ASSERT_NE(claim, nullptr);
        ASSERT_EQ(claim->review_passes.size(), 4u);
        std::vector<std::string> covered;
        for (const parser::ReviewPass& pass : claim->review_passes) {
            EXPECT_FALSE(pass.question.empty()) << pass.id;
            covered.insert(covered.end(), pass.guideline_ids.begin(), pass.guideline_ids.end());
        }
        std::vector<std::string> published = claim->guideline_ids;
        std::sort(covered.begin(), covered.end());
        std::sort(published.begin(), published.end());
        EXPECT_EQ(covered, published) << "the passes must list every claim guideline exactly once";
    }
}

namespace {

// A valid two-guideline distribution whose one profile carries `review_passes`
// as given, and optionally a retired-guideline list in sccg.compact.json -- the
// only file of the dist set that publishes one.
std::filesystem::path WritePassFixture(const std::string& name,
                                       const nlohmann::json& review_passes,
                                       const nlohmann::json& retired = nlohmann::json::array(),
                                       bool write_compact = true,
                                       const std::string& schema_version = "3.0.0") {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / ("af_sccg_dist_" + name);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    nlohmann::json profile{{"id", "claim_review"},
                           {"display_name", "Claim review"},
                           {"applies_to", nlohmann::json::array({"GSN Goal"})},
                           {"guideline_ids", nlohmann::json::array({"CL.1", "CL.2"})},
                           {"required_data", nlohmann::json::array({"SELECTED_CLAIM"})}};
    if (!review_passes.is_null())
        profile["review_passes"] = review_passes;
    std::ofstream(directory / "review_profiles.json") << nlohmann::json{
        {"schema_version", schema_version},
        {"sccg_version", "0.8.0"},
        {"review_profiles",
         nlohmann::json::array({profile})}}.dump(2);
    std::ofstream(directory / "data_packages.json") << nlohmann::json{
        {"schema_version", schema_version},
        {"sccg_version", "0.8.0"},
        {"when_unavailable", "Judge what was supplied."},
        {"data_packages",
         nlohmann::json::array(
             {nlohmann::json{{"id", "SELECTED_CLAIM"},
                             {"display_name", "Selected claim"},
                             {"role", "selected_element"},
                             {"element_role", "claim"}}})}}.dump(2);
    std::ofstream rules(directory / "ai_rule_export.jsonl");
    for (const char* id : {"CL.1", "CL.2"}) {
        rules << nlohmann::json{{"id", id},
                                {"title", std::string("Rule ") + id},
                                {"statement", "State it."},
                                {"rationale", "Because."},
                                {"category", "CL"},
                                {"schema_version", schema_version},
                                {"sccg_version", "0.8.0"}}
                     .dump()
              << "\n";
    }
    rules.close();
    if (write_compact)
        std::ofstream(directory / "sccg.compact.json") << nlohmann::json{{"retired_guidelines", retired}}.dump(2);
    return directory;
}

nlohmann::json Pass(const std::string& id, std::vector<std::string> guideline_ids) {
    return nlohmann::json{{"id", id},
                          {"display_name", id},
                          {"question", "Is it " + id + "?"},
                          {"guideline_ids", std::move(guideline_ids)}};
}

} // namespace

TEST(GuidelinesParserTest, AcceptsReviewPassesThatPartitionTheProfile) {
    const std::filesystem::path directory =
        WritePassFixture("passes_ok", nlohmann::json::array({Pass("first", {"CL.1"}), Pass("second", {"CL.2"})}));
    auto result = parser::SccgDistParser::ParseDirectory(directory);

    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error());
    const parser::ReviewProfile* profile = result.value().FindReviewProfileById("claim_review");
    ASSERT_NE(profile, nullptr);
    ASSERT_EQ(profile->review_passes.size(), 2u);
    EXPECT_EQ(profile->review_passes[1].guideline_ids, (std::vector<std::string>{"CL.2"}));
    EXPECT_EQ(result.value().when_unavailable, "Judge what was supplied.");
    std::filesystem::remove_all(directory);
}

// A fan-out review sends one request per pass. A pass partition that dropped a
// guideline would be a review that never asks about it, with nothing failing to
// say so; one listing a guideline twice would ask and report it twice.
TEST(GuidelinesParserTest, RefusesReviewPassesThatOmitAGuideline) {
    const std::filesystem::path directory =
        WritePassFixture("passes_omit", nlohmann::json::array({Pass("first", {"CL.1"}), Pass("second", {})}));
    auto result = parser::SccgDistParser::ParseDirectory(directory);
    ASSERT_FALSE(result.has_value());
    std::filesystem::remove_all(directory);

    const std::filesystem::path missing =
        WritePassFixture("passes_missing", nlohmann::json::array({Pass("first", {"CL.1"}), Pass("second", {"CL.1"})}));
    auto missing_result = parser::SccgDistParser::ParseDirectory(missing);
    ASSERT_FALSE(missing_result.has_value());
    EXPECT_NE(missing_result.error().find("more than one review pass"), std::string::npos) << missing_result.error();
    std::filesystem::remove_all(missing);
}

TEST(GuidelinesParserTest, RefusesAReviewPassListingAGuidelineTheProfileDoesNotCarry) {
    const std::filesystem::path directory = WritePassFixture(
        "passes_foreign", nlohmann::json::array({Pass("first", {"CL.1", "CL.9"}), Pass("second", {"CL.2"})}));
    auto result = parser::SccgDistParser::ParseDirectory(directory);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("CL.9"), std::string::npos) << result.error();
    std::filesystem::remove_all(directory);
}

// A retired id still in use would be two meanings for one id, and a redirect to
// a guideline that does not exist would send a stored finding nowhere.
TEST(GuidelinesParserTest, RefusesAnInconsistentRetirementList) {
    const std::filesystem::path still_published = WritePassFixture(
        "retired_published",
        nlohmann::json(nullptr),
        nlohmann::json::array({nlohmann::json{
            {"id", "CL.2"}, {"title", "x"}, {"retired_in", "0.8.0"}, {"replaced_by", {"CL.1"}}, {"note", "n"}}}));
    auto published_result = parser::SccgDistParser::ParseDirectory(still_published);
    ASSERT_FALSE(published_result.has_value());
    EXPECT_NE(published_result.error().find("both retired and published"), std::string::npos)
        << published_result.error();
    std::filesystem::remove_all(still_published);

    const std::filesystem::path dangling = WritePassFixture(
        "retired_dangling",
        nlohmann::json(nullptr),
        nlohmann::json::array({nlohmann::json{
            {"id", "AR.3"}, {"title", "x"}, {"retired_in", "0.8.0"}, {"replaced_by", {"AR.99"}}, {"note", "n"}}}));
    auto dangling_result = parser::SccgDistParser::ParseDirectory(dangling);
    ASSERT_FALSE(dangling_result.has_value());
    EXPECT_NE(dangling_result.error().find("AR.99"), std::string::npos) << dangling_result.error();
    std::filesystem::remove_all(dangling);
}

// A 3.0.0 distribution without the file carrying `retired_guidelines` is
// incomplete, and loading it would silently retire nothing -- the failure the
// runtime copy had until the build copied sccg.compact.json. Refused, loudly.
TEST(GuidelinesParserTest, RefusesAVersion3DistributionMissingItsRetirementList) {
    const std::filesystem::path directory =
        WritePassFixture("no_compact", nlohmann::json(nullptr), nlohmann::json::array(), false, "3.0.0");
    auto result = parser::SccgDistParser::ParseDirectory(directory);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("sccg.compact.json"), std::string::npos) << result.error();
    std::filesystem::remove_all(directory);
}

// A distribution that predates 0.8.0 retired nothing and splits no profile.
// Refusing it for lacking either would be the loader inventing a requirement
// the catalogue never had.
TEST(GuidelinesParserTest, LoadsADistributionThatPredatesRetirementAndPasses) {
    const std::filesystem::path directory =
        WritePassFixture("pre_080", nlohmann::json(nullptr), nlohmann::json::array(), false, "2.0.0");
    auto result = parser::SccgDistParser::ParseDirectory(directory);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error());
    EXPECT_TRUE(result.value().retired_guidelines.empty());
    EXPECT_TRUE(result.value().FindReviewProfileById("claim_review")->review_passes.empty());
    std::filesystem::remove_all(directory);
}

namespace {

// A minimal dist directory, so the consistency gate can be exercised on a
// catalog shaped wrongly on purpose. `selected_element_role` empty means the
// profile requires a supporting package and names no selected element at all.
std::filesystem::path WriteDistFixture(const std::string& name,
                                       const std::vector<std::string>& required_data,
                                       const std::vector<std::string>& package_roles,
                                       bool selected_packages_carry_an_element_role = true) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / ("af_sccg_dist_" + name);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    nlohmann::json packages = nlohmann::json::array();
    for (std::size_t index = 0; index < required_data.size(); ++index) {
        nlohmann::json package{
            {"id", required_data[index]},
            {"display_name", required_data[index]},
            {"role", package_roles[index]},
        };
        if (package_roles[index] == "selected_element" && selected_packages_carry_an_element_role) {
            package["element_role"] = "claim";
        }
        packages.push_back(std::move(package));
    }

    const nlohmann::json profiles{
        {"schema_version", "2.0.0"},
        {"sccg_version", "0.7.0"},
        {"review_profiles",
         nlohmann::json::array({nlohmann::json{{"id", "claim_review"},
                                               {"display_name", "Claim review"},
                                               {"applies_to", nlohmann::json::array({"GSN Goal"})},
                                               {"guideline_ids", nlohmann::json::array({"CL.1"})},
                                               {"required_data", required_data}}})},
    };
    std::ofstream(directory / "review_profiles.json") << profiles.dump(2);
    std::ofstream(directory / "data_packages.json")
        << nlohmann::json{{"schema_version", "2.0.0"}, {"sccg_version", "0.7.0"}, {"data_packages", packages}}.dump(2);
    std::ofstream(directory / "ai_rule_export.jsonl") << nlohmann::json{{"id", "CL.1"},
                                                                        {"title", "Write each claim"},
                                                                        {"statement", "State each claim."},
                                                                        {"rationale", "Because."},
                                                                        {"category", "CL"},
                                                                        {"schema_version", "2.0.0"},
                                                                        {"sccg_version", "0.7.0"}}
                                                             .dump()
                                                      << "\n";
    return directory;
}

} // namespace

// The failure this gate exists for was silent: SCCG 0.6.0's one generic `SEL`
// package became seven role-specific ones, and a tool that kept sending `SEL`
// went on running -- reporting the profile's real requirement as an unavailable
// *required* package on every review, with nothing failing to say so. A profile
// whose selected element cannot be named is a catalog this parser refuses.
TEST(GuidelinesParserTest, RefusesAProfileThatNamesNoSelectedElementPackage) {
    const std::filesystem::path directory = WriteDistFixture("no_selected", {"PARENT"}, {"supporting"});
    auto result = parser::SccgDistParser::ParseDirectory(directory);

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("selected-element data packages"), std::string::npos) << result.error();
    std::filesystem::remove_all(directory);
}

TEST(GuidelinesParserTest, RefusesAProfileThatNamesTwoSelectedElementPackages) {
    const std::filesystem::path directory = WriteDistFixture(
        "two_selected", {"SELECTED_CLAIM", "SELECTED_STRATEGY"}, {"selected_element", "selected_element"});
    auto result = parser::SccgDistParser::ParseDirectory(directory);

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("selected-element data packages"), std::string::npos) << result.error();
    std::filesystem::remove_all(directory);
}

// The role, not the package id, is what everything downstream matches on. A
// package carrying `role: selected_element` and no `element_role` would match
// no element at all, so every review would fail with "no SCCG review profile
// applies to the selected element type" -- a message about the argument, for a
// defect in the catalog.
TEST(GuidelinesParserTest, RefusesASelectedElementPackageWithNoElementRole) {
    const std::filesystem::path directory = WriteDistFixture("no_element_role",
                                                             {"SELECTED_CLAIM"},
                                                             {"selected_element"},
                                                             /*selected_packages_carry_an_element_role=*/false);
    auto result = parser::SccgDistParser::ParseDirectory(directory);

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("no element_role"), std::string::npos) << result.error();
    std::filesystem::remove_all(directory);
}

TEST(GuidelinesParserTest, AcceptsAProfileNamingExactlyOneSelectedElementPackage) {
    const std::filesystem::path directory =
        WriteDistFixture("one_selected", {"SELECTED_CLAIM", "PARENT"}, {"selected_element", "supporting"});
    auto result = parser::SccgDistParser::ParseDirectory(directory);

    ASSERT_TRUE(result.has_value()) << result.error();
    const parser::ReviewProfile* profile = result.value().FindReviewProfileById("claim_review");
    ASSERT_NE(profile, nullptr);
    const parser::DataPackage* selected = result.value().FindSelectedElementPackage(*profile);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->id, "SELECTED_CLAIM");
    std::filesystem::remove_all(directory);
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
