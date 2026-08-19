#include "review/sccg/sccg_prechecks.h"

#include "core/guideline_catalog.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

// Step 4 of the SCCG review workflow. SCCG publishes five deterministic
// pre-checks; both catalog parsers had read them into `GuidelinesDocument`
// since the day they were written, and nothing had ever read that vector.
//
// These tests use the real catalog, because the registry is the point: a
// hand-written fixture would prove the code can report five rows, not that it
// reports the five SCCG publishes.

namespace {

parser::SacmElement Claim(const std::string& id, const std::string& content, bool undeveloped = false) {
    parser::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    element.content = content;
    element.undeveloped = undeveloped;
    return element;
}

parser::SacmElement Strategy(const std::string& id, const std::string& content) {
    parser::SacmElement element;
    element.id = id;
    element.type = "argumentreasoning";
    element.name = id;
    element.content = content;
    return element;
}

parser::SacmElement Supports(const std::string& id, const std::string& child, const std::string& parent) {
    parser::SacmElement element;
    element.id = id;
    element.type = "assertedinference";
    element.source_refs = {child};
    element.target_refs = {parent};
    return element;
}

const review::sccg::PrecheckResult* Find(const std::vector<review::sccg::PrecheckResult>& results,
                                         const std::string& precheck_id) {
    for (const review::sccg::PrecheckResult& result : results) {
        if (result.precheck_id == precheck_id)
            return &result;
    }
    return nullptr;
}

std::vector<review::sccg::PrecheckResult>
RunFor(const parser::AssuranceCase& model, const std::string& element_id, const core::GuidelineCatalog& catalog) {
    const core::AssuranceTree tree = core::AssuranceTree::Build(model);
    return review::sccg::RunPrechecks(catalog.document, model, tree, element_id);
}

} // namespace

TEST(SccgPrechecks, ReportsEveryPrecheckTheCatalogPublishes) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;
    ASSERT_FALSE(catalog.document.prechecks.empty()) << "the catalog published no prechecks";

    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Braking is verified", true));

    const std::vector<review::sccg::PrecheckResult> results = RunFor(model, "G1", catalog);

    EXPECT_EQ(results.size(), catalog.document.prechecks.size());
    for (const parser::Precheck& precheck : catalog.document.prechecks) {
        EXPECT_NE(Find(results, precheck.id), nullptr) << precheck.id << " was not reported";
    }
}

// SCCG publishes these as candidate signals, and says so in its own words. The
// distinction between a candidate and a finding is the whole contract, so the
// interpretation travels with the result rather than being paraphrased.
TEST(SccgPrechecks, CarriesTheRegistrysOwnInterpretationAndResultType) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;

    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Braking is verified", true));

    const std::vector<review::sccg::PrecheckResult> results = RunFor(model, "G1", catalog);
    const review::sccg::PrecheckResult* strategy = Find(results, "check-explicit-strategy");
    ASSERT_NE(strategy, nullptr);

    const parser::Precheck* published = nullptr;
    for (const parser::Precheck& precheck : catalog.document.prechecks) {
        if (precheck.id == "check-explicit-strategy")
            published = &precheck;
    }
    ASSERT_NE(published, nullptr);
    EXPECT_EQ(strategy->interpretation, published->interpretation);
    EXPECT_EQ(strategy->result_type, published->result_type);
    EXPECT_EQ(strategy->guideline_ids, published->related_guideline_ids);
    EXPECT_FALSE(strategy->interpretation.empty());
}

// The check an authoring model trips most: sub-claims hung off a goal with no
// reasoning step between them.
TEST(SccgPrechecks, RaisesACandidateForADecompositionWithNoReasoningStep) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;

    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Autonomy function safety is acceptable"));
    model.elements.push_back(Claim("G2", "Perception safety is acceptable", true));
    model.elements.push_back(Claim("G3", "Planning safety is acceptable", true));
    model.elements.push_back(Supports("R1", "G2", "G1"));
    model.elements.push_back(Supports("R2", "G3", "G1"));

    const std::vector<review::sccg::PrecheckResult> results = RunFor(model, "G1", catalog);
    const review::sccg::PrecheckResult* strategy = Find(results, "check-explicit-strategy");
    ASSERT_NE(strategy, nullptr);
    EXPECT_TRUE(strategy->candidate);
    EXPECT_FALSE(strategy->detail.empty());
}

TEST(SccgPrechecks, StaysClearWhenTheDecompositionHasAStrategy) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;

    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Autonomy function safety is acceptable"));
    model.elements.push_back(Strategy("S1", "Break the claim down by UL 4600 topic"));
    model.elements.push_back(Claim("G2", "Perception safety is acceptable", true));
    model.elements.push_back(Claim("G3", "Planning safety is acceptable", true));
    model.elements.push_back(Supports("R1", "S1", "G1"));
    model.elements.push_back(Supports("R2", "G2", "S1"));
    model.elements.push_back(Supports("R3", "G3", "S1"));

    const std::vector<review::sccg::PrecheckResult> results = RunFor(model, "G1", catalog);
    const review::sccg::PrecheckResult* strategy = Find(results, "check-explicit-strategy");
    ASSERT_NE(strategy, nullptr);
    EXPECT_FALSE(strategy->candidate);
    EXPECT_FALSE(strategy->unavailable);
}

// A pre-check scoped to the reviewed element, not a sweep: the profile decides
// what is under review, and a neighbour's defect is not this review's subject.
TEST(SccgPrechecks, JudgesOnlyTheReviewedElement) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;

    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Autonomy function safety is acceptable"));
    model.elements.push_back(Claim("G2", "Perception safety is acceptable", true));
    model.elements.push_back(Claim("G3", "Planning safety is acceptable", true));
    model.elements.push_back(Supports("R1", "G2", "G1"));
    model.elements.push_back(Supports("R2", "G3", "G1"));

    // G2 is a leaf; the undecomposed-decomposition candidate belongs to G1.
    const std::vector<review::sccg::PrecheckResult> results = RunFor(model, "G2", catalog);
    const review::sccg::PrecheckResult* strategy = Find(results, "check-explicit-strategy");
    ASSERT_NE(strategy, nullptr);
    EXPECT_FALSE(strategy->candidate);
}

// "Did not fire" and "was never run" are different facts, and a reviewer must
// not read one as the other.
TEST(SccgPrechecks, ReportsAnUndecidablePrecheckAsNotRunRatherThanClear) {
    parser::GuidelinesDocument document;
    parser::Precheck invented;
    invented.id = "check-nobody-implemented";
    invented.display_name = "Something the tool cannot decide";
    invented.result_type = "boolean_candidate";
    invented.interpretation = "Candidate finding only.";
    document.prechecks.push_back(std::move(invented));
    const core::GuidelineCatalog catalog = core::BuildGuidelineCatalog(std::move(document), "test-sccg.yaml");

    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Braking is verified", true));

    const std::vector<review::sccg::PrecheckResult> results = RunFor(model, "G1", catalog);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].unavailable);
    EXPECT_FALSE(results[0].candidate);
}

// A catalog with no registry yields nothing rather than a local fallback list:
// the published set is the only authority for what a pre-check is.
TEST(SccgPrechecks, ReportsNothingWhenTheCatalogPublishesNoRegistry) {
    const core::GuidelineCatalog catalog = core::BuildGuidelineCatalog(parser::GuidelinesDocument{}, "test-sccg.yaml");

    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Braking is verified", true));

    EXPECT_TRUE(RunFor(model, "G1", catalog).empty());
}
