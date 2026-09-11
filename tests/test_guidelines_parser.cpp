// The SCCG catalogue loader.
//
// Since SCCG contract 3.1.0 the catalogue is read from one file,
// `dist/sccg.full.json`, which SCCG declares sufficient on its own for review,
// authoring and retirement. This tool used to keep two loaders -- the
// per-concern dist files and the whole-catalogue YAML -- and every contract
// addition was implemented and tested twice; one of the two also silently
// retired nothing, because the files it read did not carry the list.

#include "parser/sccg_dist_parser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace {

std::filesystem::path RepositorySccgDistPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "external" / "safety-case-core-guidelines" /
           "dist";
}

std::filesystem::path RepositorySccgSchemasPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "external" / "safety-case-core-guidelines" /
           "schemas";
}

const parser::GuidelinesDocument& ReleasedCatalogue() {
    static const parser::GuidelinesDocument document = [] {
        auto result = parser::SccgDistParser::ParseDirectory(RepositorySccgDistPath());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error());
        return result.has_value() ? *result : parser::GuidelinesDocument{};
    }();
    return document;
}

std::set<std::string> AsSet(const std::vector<std::string>& values) {
    return {values.begin(), values.end()};
}

std::set<std::string> AsSet(const nlohmann::json& values) {
    std::set<std::string> result;
    for (const nlohmann::json& value : values)
        result.insert(value.get<std::string>());
    return result;
}

// A minimal valid catalogue: two guidelines, one profile, one selected-element
// package. Each refusal test changes exactly one thing through `mutate`.
nlohmann::json MinimalCatalogue() {
    return nlohmann::json{
        {"schema_version", "3.1.0"},
        {"sccg_version", "0.9.0"},
        {"document", {{"title", "Fixture catalogue"}, {"license", {{"id", "CC-BY-4.0"}}}}},
        {"guidelines",
         nlohmann::json::array({
             {{"id", "CL.1"},
              {"category", "CL"},
              {"title", "Rule one"},
              {"statement", "State it."},
              {"rationale", "Because."},
              {"references", nlohmann::json::array({{{"source_id", "UL4600"}}})}},
             {{"id", "CL.2"},
              {"category", "CL"},
              {"title", "Rule two"},
              {"statement", "State it."},
              {"rationale", "Because."}},
         })},
        {"review_profiles",
         nlohmann::json::array({{{"id", "claim_review"},
                                 {"display_name", "Claim review"},
                                 {"applies_to", nlohmann::json::array({"GSN Goal"})},
                                 {"guideline_ids", nlohmann::json::array({"CL.1", "CL.2"})},
                                 {"required_data", nlohmann::json::array({"SELECTED_CLAIM"})},
                                 {"optional_data", nlohmann::json::array({"PARENT"})}}})},
        {"data_packages",
         nlohmann::json::array({
             {{"id", "SELECTED_CLAIM"},
              {"display_name", "Selected claim"},
              {"role", "selected_element"},
              {"element_role", "claim"},
              {"required_fields", nlohmann::json::array({"element_id"})},
              {"field_meanings", {{"element_id", "The reviewed element."}}}},
             {{"id", "PARENT"}, {"display_name", "Parent"}, {"role", "supporting"}},
         })},
        {"when_unavailable", "Judge what was supplied."},
        {"review_pass_instruction", "The question for this pass is: {question}"},
        {"retired_guidelines", nlohmann::json::array()},
    };
}

std::filesystem::path WriteCatalogue(const std::string& name, const std::function<void(nlohmann::json&)>& mutate = {}) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / ("af_sccg_catalogue_" + name);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    nlohmann::json catalogue = MinimalCatalogue();
    if (mutate)
        mutate(catalogue);
    std::ofstream(directory / "sccg.full.json") << catalogue.dump(2);
    return directory;
}

nlohmann::json Pass(const std::string& id, std::vector<std::string> guideline_ids) {
    return nlohmann::json{{"id", id},
                          {"display_name", id},
                          {"question", "Is it " + id + "?"},
                          {"guideline_ids", std::move(guideline_ids)}};
}

// Loads the fixture and expects a refusal whose message contains `expected`.
void ExpectRefused(const std::string& name,
                   const std::function<void(nlohmann::json&)>& mutate,
                   const std::string& expected) {
    SCOPED_TRACE(name);
    const std::filesystem::path directory = WriteCatalogue(name, mutate);
    const auto result = parser::SccgDistParser::ParseDirectory(directory);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find(expected), std::string::npos) << result.error();
    std::filesystem::remove_all(directory);
}

} // namespace

// ---- The released catalogue -------------------------------------------------

TEST(GuidelinesParserTest, ParsesTheReleasedCatalogue) {
    const parser::GuidelinesDocument& catalogue = ReleasedCatalogue();
    EXPECT_EQ(catalogue.schema_version, "3.1.0");
    EXPECT_EQ(catalogue.sccg_version, "0.9.0");
    EXPECT_EQ(catalogue.guidelines.size(), 45u);
    EXPECT_EQ(catalogue.review_profiles.size(), 7u);
    EXPECT_FALSE(catalogue.data_packages.empty());
    EXPECT_FALSE(catalogue.prechecks.empty());

    EXPECT_EQ(catalogue.metadata.title, "Safety Case Core Guidelines");
    EXPECT_FALSE(catalogue.metadata.purpose.empty());
    EXPECT_EQ(catalogue.metadata.license.id, "CC-BY-4.0");

    const parser::Guideline* cl1 = catalogue.FindGuidelineById("CL.1");
    ASSERT_NE(cl1, nullptr);
    EXPECT_EQ(cl1->title, "Write each claim as a falsifiable proposition");
    EXPECT_FALSE(cl1->rationale.empty());
    EXPECT_FALSE(cl1->tool.applicable_elements.empty());
    EXPECT_FALSE(catalogue.FindGuidelinesByApplicableElement("GSN Goal").empty());
    const std::vector<const parser::Guideline*> proposition =
        catalogue.FindGuidelinesBySuggestedCheckId("check-claim-is-proposition");
    ASSERT_FALSE(proposition.empty());
    EXPECT_EQ(proposition.front()->id, "CL.1");

    const parser::ReviewProfile* strategy = catalogue.FindReviewProfileById("strategy_review");
    ASSERT_NE(strategy, nullptr);
    for (const std::string element : {"GSN Strategy", "CAE Argument"})
        EXPECT_NE(std::find(strategy->applies_to.begin(), strategy->applies_to.end(), element),
                  strategy->applies_to.end())
            << element;

    // Every profile names exactly one selected-element package, and its role is
    // one the catalogue publishes as selectable -- the positive half of the gate
    // the refusal tests below exercise.
    for (const parser::ReviewProfile& profile : catalogue.review_profiles) {
        SCOPED_TRACE(profile.id);
        const parser::DataPackage* selected = catalogue.FindSelectedElementPackage(profile);
        ASSERT_NE(selected, nullptr);
        bool selectable = false;
        for (const parser::SelectableElement& element : catalogue.selectable_elements)
            selectable = selectable || element.element_role == selected->element_role;
        EXPECT_TRUE(selectable) << selected->element_role;
    }

    // The tool contract the review method and the staged checks read.
    const parser::Guideline* cl5 = catalogue.FindGuidelineById("CL.5");
    ASSERT_NE(cl5, nullptr);
    EXPECT_FALSE(cl5->short_rule.empty());
    EXPECT_TRUE(std::any_of(cl5->tool.markers.begin(), cl5->tool.markers.end(), [](const parser::GuidelineMarker& m) {
        return m.effect == "suppress";
    }));
    const parser::Guideline* cl3 = catalogue.FindGuidelineById("CL.3");
    ASSERT_NE(cl3, nullptr);
    ASSERT_FALSE(cl3->tool.thresholds.empty());
    EXPECT_EQ(cl3->tool.thresholds[0].id, "claim_word_count");
    const parser::Guideline* ar2 = catalogue.FindGuidelineById("AR.2");
    ASSERT_NE(ar2, nullptr);
    ASSERT_FALSE(ar2->tool.repair.empty());
    EXPECT_EQ(ar2->tool.repair[0].action, "add_element");
    EXPECT_EQ(ar2->tool.repair[0].element_role, "strategy");
    for (const char* state : {"available", "not_implemented", "empty", "withheld"})
        EXPECT_NE(catalogue.FindAvailabilityStateById(state), nullptr) << state;
    for (const parser::Precheck& precheck : catalogue.prechecks)
        EXPECT_FALSE(precheck.fires_when.empty()) << precheck.id;

    // No profile silences a guideline when a package is absent any more; the
    // registry-wide rule governs, and EVIDENCE_BASIS is optional to evidence
    // review (safety-case-core-guidelines#13).
    const parser::ReviewProfile* evidence = catalogue.FindReviewProfileById("evidence_review");
    ASSERT_NE(evidence, nullptr);
    EXPECT_TRUE(evidence->when_absent.empty());
    EXPECT_NE(std::find(evidence->optional_data.begin(), evidence->optional_data.end(), "EVIDENCE_BASIS"),
              evidence->optional_data.end());

    // Authoring guidance sits under `authoring_guidance` in the whole file, and
    // its published element rules agree with the profile the registries derive.
    EXPECT_FALSE(catalogue.authoring_guidance.core_rules.empty());
    EXPECT_FALSE(catalogue.authoring_guidance.usage.empty());
    ASSERT_FALSE(catalogue.authoring_guidance.element_rules.empty());
    for (const parser::AuthoringElementRule& rule : catalogue.authoring_guidance.element_rules) {
        SCOPED_TRACE(rule.element_role);
        const parser::ReviewProfile* derived = catalogue.FindReviewProfileForElementRole(rule.element_role);
        ASSERT_NE(derived, nullptr);
        EXPECT_EQ(derived->id, rule.review_profile_id);
    }
    EXPECT_EQ(catalogue.FindReviewProfileForElementRole("nonesuch"), nullptr);
}

// The whole-catalogue guideline record carries no copy of the per-guideline
// convenience fields SCCG's rule export carries; the loader derives them. The
// derivation is only correct if it matches what SCCG itself published, so this
// holds every one of them, for every guideline, against the export file.
TEST(GuidelinesParserTest, DerivesEveryPerGuidelineFieldExactlyAsSccgsExportPublishesIt) {
    const parser::GuidelinesDocument& catalogue = ReleasedCatalogue();
    std::ifstream export_file(RepositorySccgDistPath() / "ai_rule_export.jsonl");
    ASSERT_TRUE(export_file) << "the release ships the rule export this is checked against";

    std::size_t compared = 0;
    std::string line;
    while (std::getline(export_file, line)) {
        if (line.empty())
            continue;
        const nlohmann::json row = nlohmann::json::parse(line);
        const std::string id = row.at("id");
        SCOPED_TRACE(id);
        const parser::Guideline* guideline = catalogue.FindGuidelineById(id);
        ASSERT_NE(guideline, nullptr);
        EXPECT_EQ(guideline->rule_id, row.at("rule_id").get<std::string>());
        EXPECT_EQ(guideline->category_id, row.at("category_id").get<std::string>());
        EXPECT_EQ(AsSet(guideline->review_profile_ids), AsSet(row.at("review_profile_ids")));
        EXPECT_EQ(AsSet(guideline->data_package_ids), AsSet(row.at("data_package_ids")));
        EXPECT_EQ(AsSet(guideline->reference_source_ids), AsSet(row.at("reference_source_ids")));
        std::set<std::string> distinctions;
        for (const parser::GuidelineDistinction& distinction : guideline->distinguish_from)
            distinctions.insert(distinction.id);
        std::set<std::string> published;
        for (const nlohmann::json& distinction : row.at("distinguish_from"))
            published.insert(distinction.at("id").get<std::string>());
        EXPECT_EQ(distinctions, published);
        ++compared;
    }
    EXPECT_EQ(compared, catalogue.guidelines.size());
}

// Contract 3.x: what a review tool must read, all from the one file.
TEST(GuidelinesParserTest, ReadsTheSccgContractFromTheOneFile) {
    const parser::GuidelinesDocument& catalogue = ReleasedCatalogue();

    // 0.9.0: an empty package is a fact the review may rely on; unimplemented
    // and withheld ones tell it nothing.
    EXPECT_NE(catalogue.when_unavailable.find("may rely on that"), std::string::npos) << catalogue.when_unavailable;
    EXPECT_NE(catalogue.when_unavailable.find("never silences a guideline"), std::string::npos);

    // The sentence a tool sends with each review pass.
    const std::string& instruction = catalogue.review_pass_instruction;
    ASSERT_NE(instruction.find("{question}"), std::string::npos);
    EXPECT_EQ(instruction.find("{question}"), instruction.rfind("{question}"));

    // Every published field has a meaning.
    for (const parser::DataPackage& package : catalogue.data_packages) {
        for (const std::vector<std::string>* fields : {&package.required_fields, &package.optional_fields}) {
            for (const std::string& field : *fields)
                EXPECT_TRUE(package.field_meanings.contains(field)) << package.id << "." << field;
        }
    }
    const parser::DataPackage* history = catalogue.FindDataPackageById("CHANGE_HISTORY");
    ASSERT_NE(history, nullptr);
    EXPECT_TRUE(history->field_meanings.contains("prior_findings"));

    // Retired guidelines, each redirecting to live ones. Findable only as
    // retired: a retired id is never reviewed against.
    for (const char* retired_id : {"AR.3", "SU.9", "RD.6"}) {
        SCOPED_TRACE(retired_id);
        EXPECT_EQ(catalogue.FindGuidelineById(retired_id), nullptr);
        const parser::RetiredGuideline* retired = catalogue.FindRetiredGuidelineById(retired_id);
        ASSERT_NE(retired, nullptr);
        for (const std::string& replacement : retired->replaced_by)
            EXPECT_NE(catalogue.FindGuidelineById(replacement), nullptr) << replacement;
    }

    // Disambiguation for the pairs observed to be confused.
    const parser::Guideline* cl4 = catalogue.FindGuidelineById("CL.4");
    ASSERT_NE(cl4, nullptr);
    std::set<std::string> neighbours;
    for (const parser::GuidelineDistinction& distinction : cl4->distinguish_from)
        neighbours.insert(distinction.id);
    EXPECT_TRUE(neighbours.contains("CL.5"));
    EXPECT_TRUE(neighbours.contains("AR.4"));

    // claim_review in passes that partition it exactly.
    const parser::ReviewProfile* claim = catalogue.FindReviewProfileById("claim_review");
    ASSERT_NE(claim, nullptr);
    ASSERT_EQ(claim->review_passes.size(), 4u);
    std::vector<std::string> covered;
    for (const parser::ReviewPass& pass : claim->review_passes)
        covered.insert(covered.end(), pass.guideline_ids.begin(), pass.guideline_ids.end());
    std::sort(covered.begin(), covered.end());
    std::vector<std::string> published = claim->guideline_ids;
    std::sort(published.begin(), published.end());
    EXPECT_EQ(covered, published);
}

// ---- The loader on a minimal catalogue --------------------------------------

TEST(GuidelinesParserTest, LoadsAMinimalCatalogueAndDerivesItsLinks) {
    const std::filesystem::path directory = WriteCatalogue("minimal");
    auto result = parser::SccgDistParser::ParseDirectory(directory);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error());
    const parser::Guideline* cl1 = result->FindGuidelineById("CL.1");
    ASSERT_NE(cl1, nullptr);
    EXPECT_EQ(cl1->rule_id, "CL.1");
    EXPECT_EQ(cl1->review_profile_ids, (std::vector<std::string>{"claim_review"}));
    EXPECT_EQ(cl1->data_package_ids, (std::vector<std::string>{"PARENT", "SELECTED_CLAIM"}));
    EXPECT_EQ(cl1->reference_source_ids, (std::vector<std::string>{"UL4600"}));
    EXPECT_EQ(cl1->schema_version, "3.1.0");
    EXPECT_EQ(result->review_pass_instruction, "The question for this pass is: {question}");
    EXPECT_EQ(result->FindDataPackageById("SELECTED_CLAIM")->field_meanings.at("element_id"), "The reviewed element.");
    std::filesystem::remove_all(directory);
}

TEST(GuidelinesParserTest, RefusesAMissingOrMalformedCatalogue) {
    const std::filesystem::path empty = std::filesystem::temp_directory_path() / "af_sccg_catalogue_none";
    std::filesystem::remove_all(empty);
    std::filesystem::create_directories(empty);
    auto missing = parser::SccgDistParser::ParseDirectory(empty);
    ASSERT_FALSE(missing.has_value());
    EXPECT_NE(missing.error().find("sccg.full.json"), std::string::npos) << missing.error();

    std::ofstream(empty / "sccg.full.json") << "{ not json";
    EXPECT_FALSE(parser::SccgDistParser::ParseDirectory(empty).has_value());
    std::filesystem::remove_all(empty);
}

// A minor only adds keys and fields, so it is compared by major -- SCCG's own
// changelog says so. Another major may restructure what this loader reads.
TEST(GuidelinesParserTest, ReadsAnyContract3MinorAndRefusesAnotherMajor) {
    const std::filesystem::path later_minor =
        WriteCatalogue("minor", [](nlohmann::json& catalogue) { catalogue["schema_version"] = "3.9.0"; });
    EXPECT_TRUE(parser::SccgDistParser::ParseDirectory(later_minor).has_value());
    std::filesystem::remove_all(later_minor);

    ExpectRefused("major2", [](nlohmann::json& catalogue) { catalogue["schema_version"] = "2.0.0"; }, "not supported");
    ExpectRefused("major4", [](nlohmann::json& catalogue) { catalogue["schema_version"] = "4.0.0"; }, "not supported");
}

TEST(GuidelinesParserTest, RefusesAGuidelineMissingItsIdentity) {
    ExpectRefused("no_id", [](nlohmann::json& catalogue) { catalogue["guidelines"][1].erase("id"); }, "missing id");
}

// A fan-out review sends one request per pass. A partition that dropped a
// guideline would be a review that never asks about it; one listing a guideline
// twice would ask and report it twice.
TEST(GuidelinesParserTest, AcceptsReviewPassesThatPartitionTheProfile) {
    const std::filesystem::path directory = WriteCatalogue("passes_ok", [](nlohmann::json& catalogue) {
        catalogue["review_profiles"][0]["review_passes"] =
            nlohmann::json::array({Pass("first", {"CL.1"}), Pass("second", {"CL.2"})});
    });
    auto result = parser::SccgDistParser::ParseDirectory(directory);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error());
    ASSERT_EQ(result->FindReviewProfileById("claim_review")->review_passes.size(), 2u);
    std::filesystem::remove_all(directory);
}

TEST(GuidelinesParserTest, RefusesReviewPassesThatDoNotPartitionTheProfile) {
    ExpectRefused(
        "passes_twice",
        [](nlohmann::json& catalogue) {
            catalogue["review_profiles"][0]["review_passes"] =
                nlohmann::json::array({Pass("first", {"CL.1"}), Pass("second", {"CL.1", "CL.2"})});
        },
        "more than one review pass");
    ExpectRefused(
        "passes_omit",
        [](nlohmann::json& catalogue) {
            catalogue["review_profiles"][0]["review_passes"] =
                nlohmann::json::array({Pass("first", {"CL.1"}), Pass("second", {"CL.1"})});
        },
        "more than one review pass");
    ExpectRefused(
        "passes_missing",
        [](nlohmann::json& catalogue) {
            catalogue["review_profiles"][0]["review_passes"] = nlohmann::json::array({Pass("only", {"CL.1"})});
        },
        "omit guideline 'CL.2'");
    ExpectRefused(
        "passes_foreign",
        [](nlohmann::json& catalogue) {
            catalogue["review_profiles"][0]["review_passes"] =
                nlohmann::json::array({Pass("first", {"CL.1", "CL.9"}), Pass("second", {"CL.2"})});
        },
        "CL.9");
}

// A retired id still in use is two meanings for one id; a redirect to a
// guideline that does not exist sends a stored finding nowhere.
TEST(GuidelinesParserTest, RefusesAnInconsistentRetirementList) {
    ExpectRefused(
        "retired_published",
        [](nlohmann::json& catalogue) {
            catalogue["retired_guidelines"] = nlohmann::json::array(
                {{{"id", "CL.2"}, {"title", "x"}, {"retired_in", "0.8.0"}, {"replaced_by", {"CL.1"}}, {"note", "n"}}});
        },
        "both retired and published");
    ExpectRefused(
        "retired_dangling",
        [](nlohmann::json& catalogue) {
            catalogue["retired_guidelines"] = nlohmann::json::array(
                {{{"id", "AR.3"}, {"title", "x"}, {"retired_in", "0.8.0"}, {"replaced_by", {"AR.99"}}, {"note", "n"}}});
        },
        "AR.99");
}

TEST(GuidelinesParserTest, RefusesADistinctionNamingAnUnknownGuideline) {
    ExpectRefused(
        "distinction_unknown",
        [](nlohmann::json& catalogue) {
            catalogue["guidelines"][0]["distinguish_from"] =
                nlohmann::json::array({{{"id", "ZZ.1"}, {"note", "Cite ZZ.1 when..."}}});
        },
        "ZZ.1");
}

// SCCG requires `{question}` exactly once and no other brace. A tool that
// substituted into anything else would send the model a literal placeholder.
TEST(GuidelinesParserTest, RefusesAPassInstructionWithoutExactlyOnePlaceholder) {
    for (const char* bad :
         {"No placeholder here.", "{question} and {question}", "{{question}}", "{question} {other}"}) {
        SCOPED_TRACE(bad);
        const std::string instruction = bad;
        ExpectRefused(
            "instruction",
            [&](nlohmann::json& catalogue) { catalogue["review_pass_instruction"] = instruction; },
            "{question}");
    }
}

// The failure this gate exists for was silent: SCCG 0.6.0's one generic `SEL`
// package became seven role-specific ones, and a tool that kept sending `SEL`
// went on running, reporting the profile's real requirement as an unavailable
// required package on every review. A profile whose selected element cannot be
// named is a catalogue this loader refuses.
TEST(GuidelinesParserTest, RefusesAProfileWithoutExactlyOneSelectedElementPackage) {
    ExpectRefused(
        "no_selected",
        [](nlohmann::json& catalogue) {
            catalogue["review_profiles"][0]["required_data"] = nlohmann::json::array({"PARENT"});
        },
        "selected-element data packages");
    ExpectRefused(
        "two_selected",
        [](nlohmann::json& catalogue) {
            catalogue["data_packages"].push_back({{"id", "SELECTED_STRATEGY"},
                                                  {"display_name", "Selected strategy"},
                                                  {"role", "selected_element"},
                                                  {"element_role", "strategy"}});
            catalogue["review_profiles"][0]["required_data"] =
                nlohmann::json::array({"SELECTED_CLAIM", "SELECTED_STRATEGY"});
        },
        "selected-element data packages");
    ExpectRefused(
        "selected_no_role",
        [](nlohmann::json& catalogue) { catalogue["data_packages"][0].erase("element_role"); },
        "carries no element_role");
}

TEST(GuidelinesParserTest, SccgSchemaContractsArePresentAndReadable) {
    for (const std::string schema_file : {"review_profiles.schema.json",
                                          "data_packages.schema.json",
                                          "ai_rule_export.schema.json",
                                          "prechecks.schema.json",
                                          "sccg.schema.json"}) {
        const std::filesystem::path path = RepositorySccgSchemasPath() / schema_file;
        ASSERT_TRUE(std::filesystem::exists(path)) << path.string();
        std::ifstream input(path);
        nlohmann::json schema;
        ASSERT_NO_THROW(input >> schema) << path.string();
        EXPECT_EQ(schema.value("$schema", ""), "http://json-schema.org/draft-07/schema#");
        EXPECT_TRUE(schema.contains("required")) << path.string();
    }
}
