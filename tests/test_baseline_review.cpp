// The evaluation harness's --baseline control: an AI review request with nothing
// from SCCG in it, reading the same argument an SCCG review reads.
//
// The comparison it serves measures SCCG only if the two requests differ in
// SCCG alone. These tests hold the baseline to the elements an SCCG review of
// the same element reads, to the SCCG review's system instruction, and free of
// the catalogue.

#include "eval/baseline_review.h"

#include "core/assurance_tree.h"
#include "core/guideline_catalog.h"
#include "review/sccg/sccg_review.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

parser::SacmElement MakeElement(const std::string& id,
                                const std::string& type,
                                const std::string& name,
                                const std::string& content = {},
                                const std::string& description = {}) {
    parser::SacmElement element;
    element.id = id;
    element.type = type;
    element.name = name;
    element.content = content;
    element.description = description;
    return element;
}

parser::SacmElement MakeRelationship(const std::string& id,
                                     const std::string& type,
                                     std::vector<std::string> sources,
                                     std::vector<std::string> targets) {
    parser::SacmElement relationship;
    relationship.id = id;
    relationship.type = type;
    relationship.source_refs = std::move(sources);
    relationship.target_refs = std::move(targets);
    return relationship;
}

// A goal under a goal, with context on the top goal and evidence below the
// leaf: parent, children, direct and inherited context and an evidence path
// each have something in them for one of the elements reviewed.
parser::AssuranceCase ArgumentFragment() {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("G1", "claim", "Top", "The blender is acceptably safe."));
    assurance_case.elements.push_back(MakeElement("C1", "artifact", "Context", {}, "Domestic use by adults."));
    assurance_case.elements.push_back(MakeElement("G2", "claim", "Sub", "The motor unit is acceptably safe."));
    assurance_case.elements.push_back(MakeElement("G3", "claim", "Leaf", "Blade contact is prevented."));
    assurance_case.elements.push_back(MakeElement("E1", "artifactreference", "Evidence", "BL-TR-100 rev. A, table 1."));
    assurance_case.elements.push_back(MakeRelationship("INF1", "assertedinference", {"G2"}, {"G1"}));
    assurance_case.elements.push_back(MakeRelationship("CTX1", "assertedcontext", {"C1"}, {"G1"}));
    assurance_case.elements.push_back(MakeRelationship("INF2", "assertedinference", {"G3"}, {"G2"}));
    assurance_case.elements.push_back(MakeRelationship("EV1", "assertedevidence", {"E1"}, {"G3"}));
    return assurance_case;
}

const parser::GuidelinesDocument& Catalog() {
    static const parser::GuidelinesDocument document = [] {
        core::GuidelineCatalog catalog;
        std::string error;
        EXPECT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;
        return catalog.document;
    }();
    return document;
}

std::vector<std::string> Sorted(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    return values;
}

} // namespace

// The fairness the comparison rests on: whatever an SCCG review of an element
// is shown of the argument around it, the baseline is shown too, and nothing
// more.
TEST(BaselineReviewTest, ReadsTheSameElementsAnSccgReviewReads) {
    const parser::AssuranceCase assurance_case = ArgumentFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);

    for (const std::string element_id : {"G1", "G2", "G3", "E1"}) {
        SCOPED_TRACE(element_id);
        eval::BaselineReviewRequest baseline;
        std::string error;
        ASSERT_TRUE(eval::BuildBaselineReviewRequest(assurance_case, tree, element_id, baseline, error)) << error;

        review::AiReviewPayload payload;
        ASSERT_TRUE(review::BuildAiReviewPayload(assurance_case, tree, element_id, payload, error)) << error;
        review::AiReviewDataPackageBundle packages;
        ASSERT_TRUE(
            review::CollectAiReviewDataPackages(assurance_case, tree, element_id, Catalog(), nullptr, packages, error))
            << error;

        EXPECT_EQ(Sorted(baseline.reviewed_element_ids), Sorted(review::ReviewedElementIds(payload, packages)));
    }
}

TEST(BaselineReviewTest, SendsTheSccgSystemInstructionAndNothingFromTheCatalogue) {
    const parser::AssuranceCase assurance_case = ArgumentFragment();
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    eval::BaselineReviewRequest baseline;
    std::string error;
    ASSERT_TRUE(eval::BuildBaselineReviewRequest(assurance_case, tree, "G2", baseline, error)) << error;

    review::AiReviewPayload payload;
    ASSERT_TRUE(review::BuildAiReviewPayload(assurance_case, tree, "G2", payload, error)) << error;
    const review::AiReviewRequestArtifacts sccg = review::BuildAiReviewRequestArtifacts(payload, {});
    EXPECT_EQ(baseline.system_instruction, sccg.systemInstruction);

    ASSERT_EQ(baseline.prompt_segments.size(), 2u);
    EXPECT_EQ(baseline.prompt, baseline.prompt_segments[0] + baseline.prompt_segments[1]);
    EXPECT_NE(baseline.prompt.find("The motor unit is acceptably safe."), std::string::npos);
    EXPECT_NE(baseline.prompt.find("Domestic use by adults."), std::string::npos);
    EXPECT_NE(baseline.prompt.find("BL-TR-100 rev. A, table 1."), std::string::npos);

    for (const char* sccg_term : {"SCCG", "sccg", "guideline", "profile", "SELECTED_", "data package", "pre-check"}) {
        SCOPED_TRACE(sccg_term);
        EXPECT_EQ(baseline.prompt.find(sccg_term), std::string::npos);
    }
    for (const parser::Guideline& guideline : Catalog().guidelines) {
        SCOPED_TRACE(guideline.id);
        EXPECT_EQ(baseline.prompt.find(guideline.id), std::string::npos);
    }
}

TEST(BaselineReviewTest, RefusesWhatAnSccgReviewRefuses) {
    parser::AssuranceCase assurance_case = ArgumentFragment();
    assurance_case.elements.push_back(MakeElement("X1", "activity", "Activity", "Work item."));
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    eval::BaselineReviewRequest baseline;
    std::string error;

    EXPECT_FALSE(eval::BuildBaselineReviewRequest(assurance_case, tree, "missing", baseline, error));
    EXPECT_EQ(error, "Selected element was not found.");
    EXPECT_FALSE(eval::BuildBaselineReviewRequest(assurance_case, tree, "X1", baseline, error));
    EXPECT_EQ(error, "AI Review does not support the selected element type.");
}

// A judge reads findings from both setups, so a baseline finding is laid out
// the way an SCCG finding's message is, and an empty one is not a finding.
TEST(BaselineReviewTest, ParsesFindingsIntoTheLayoutAnSccgFindingHas) {
    const eval::BaselineReviewParseResult parsed = eval::ParseBaselineReviewResponse(
        "```json\n"
        R"({"reviewed_element_id": "G2", "findings": [)"
        R"({"message": "The scope of use is unstated.", "why_it_matters": "A reader cannot tell where it holds.",)"
        R"( "suggested_fix": "State the intended use.", "confidence": "high"}, {"message": ""}]})"
        "\n```");
    ASSERT_TRUE(parsed.error_message.empty()) << parsed.error_message;
    EXPECT_EQ(parsed.reviewed_element_id, "G2");
    ASSERT_EQ(parsed.findings.size(), 1u);
    EXPECT_EQ(parsed.findings[0].confidence, "high");
    EXPECT_EQ(parsed.findings[0].message,
              "The scope of use is unstated.\n\nWhy it matters:\nA reader cannot tell where it holds.\n\n"
              "Suggested fix:\nState the intended use.\n\nConfidence:\nhigh");
}

TEST(BaselineReviewTest, RefusesAResponseWithoutAFindingsArray) {
    EXPECT_FALSE(eval::ParseBaselineReviewResponse("No problems found.").error_message.empty());
    EXPECT_FALSE(eval::ParseBaselineReviewResponse(R"({"reviewed_element_id": "G2"})").error_message.empty());

    const eval::BaselineReviewParseResult none = eval::ParseBaselineReviewResponse(R"({"findings": []})");
    EXPECT_TRUE(none.error_message.empty()) << none.error_message;
    EXPECT_TRUE(none.findings.empty());
}
