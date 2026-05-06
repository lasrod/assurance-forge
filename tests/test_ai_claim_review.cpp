#include "ai/ai_claim_review.h"
#include "core/assurance_tree.h"

#include <algorithm>
#include <gtest/gtest.h>
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

parser::Guideline MakeClaimGuideline(const std::string& id, const std::string& title) {
    parser::Guideline guideline;
    guideline.id = id;
    guideline.category = "CL";
    guideline.title = title;
    guideline.statement = "Write a clear claim.";
    guideline.rationale = "Reviewers need clear claims.";
    guideline.review_prompts = {"Is the claim reviewable?"};
    guideline.examples.good = "The braking controller response time meets the acceptance criterion.";
    guideline.tool.applicable_elements = {"GSN Goal", "SACM Claim"};
    guideline.tool.detection_hints = {"Look for vague claim text."};
    return guideline;
}

parser::ReviewProfile MakeClaimWordingProfile() {
    parser::ReviewProfile profile;
    profile.id = "claim_wording_review";
    profile.display_name = "Claim wording review";
    profile.description = "Reviews claim wording.";
    profile.guideline_ids = {"CL.1"};
    return profile;
}

bool HasPackage(const ai::AiReviewDataPackageBundle& packages, const std::string& id) {
    return std::find_if(packages.available.begin(),
                        packages.available.end(),
                        [&](const ai::AiReviewDataPackage& package) { return package.id == id; }) !=
           packages.available.end();
}

bool HasUnavailablePackage(const ai::AiReviewDataPackageBundle& packages, const std::string& id) {
    return std::find_if(packages.unavailable.begin(), packages.unavailable.end(), [&](const auto& package) {
               return package.id == id;
           }) != packages.unavailable.end();
}

} // namespace

TEST(AiClaimReviewTest, BuildsSelectedParentAndDirectChildrenPayload) {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("G1", "claim", "Top goal", "System is safe."));
    assurance_case.elements.push_back(MakeElement("G2", "claim", "Sub goal", "Braking is safe."));
    assurance_case.elements.push_back(MakeElement("CTX1", "artifact", "Context", {}, "Operational design domain."));
    assurance_case.elements.push_back(MakeRelationship("INF1", "assertedinference", {"G2"}, {"G1"}));
    assurance_case.elements.push_back(MakeRelationship("CTXREL1", "assertedcontext", {"CTX1"}, {"G1"}));

    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    ai::AiReviewPayload payload;
    std::string error;

    ASSERT_TRUE(ai::BuildAiReviewPayload(assurance_case, tree, "G1", payload, error)) << error;
    EXPECT_EQ(payload.selected.id, "G1");
    EXPECT_EQ(payload.selected.role, "selected");
    EXPECT_EQ(payload.selected.type, "GSN Goal / SACM Claim");
    EXPECT_FALSE(payload.parent.has_value());
    ASSERT_EQ(payload.children.size(), 2u);
    EXPECT_EQ(payload.children[0].role, "child");
}

TEST(AiClaimReviewTest, CollectsSelectedParentChildrenAndContextDataPackages) {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("G1", "claim", "Top goal", "System is safe."));
    assurance_case.elements.push_back(MakeElement("G2", "claim", "Sub goal", "Braking is safe."));
    assurance_case.elements.push_back(MakeElement("G3", "claim", "Leaf goal", "Brake timing is safe."));
    assurance_case.elements.push_back(MakeElement("CTX1", "artifact", "Context", {}, "Operational design domain."));
    assurance_case.elements.push_back(MakeRelationship("INF1", "assertedinference", {"G2"}, {"G1"}));
    assurance_case.elements.push_back(MakeRelationship("INF2", "assertedinference", {"G3"}, {"G2"}));
    assurance_case.elements.push_back(MakeRelationship("CTXREL1", "assertedcontext", {"CTX1"}, {"G2"}));

    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    parser::ReviewProfile profile;
    profile.id = "decomposition_review";
    profile.required_data = {"SEL", "PARENT", "CHILDREN", "DIRECT_CONTEXT", "EVIDENCE_PATH"};
    profile.optional_data = {"PROJECT_GLOSSARY"};

    ai::AiReviewDataPackageBundle packages;
    std::string error;
    ASSERT_TRUE(ai::CollectAiReviewDataPackages(assurance_case, tree, "G2", &profile, packages, error)) << error;

    EXPECT_TRUE(HasPackage(packages, "SEL"));
    EXPECT_TRUE(HasPackage(packages, "PARENT"));
    EXPECT_TRUE(HasPackage(packages, "CHILDREN"));
    EXPECT_TRUE(HasPackage(packages, "DIRECT_CONTEXT"));
    EXPECT_TRUE(HasUnavailablePackage(packages, "EVIDENCE_PATH"));
    EXPECT_TRUE(HasUnavailablePackage(packages, "PROJECT_GLOSSARY"));
}

TEST(AiClaimReviewTest, RejectsUnsupportedElements) {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("A1", "activity", "Activity", "Work item."));
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    ai::AiReviewPayload payload;
    std::string error;

    EXPECT_FALSE(ai::BuildAiReviewPayload(assurance_case, tree, "A1", payload, error));
    EXPECT_EQ(error, "AI Review does not support the selected element type.");
}

TEST(AiClaimReviewTest, MapsStrategyAndEvidenceToSccgAppliesToNames) {
    parser::SacmElement strategy = MakeElement("S1", "argumentreasoning", "Strategy", "By decomposition.");
    std::vector<std::string> strategy_names = ai::SccgAppliesToNamesForElement(strategy);
    EXPECT_NE(std::find(strategy_names.begin(), strategy_names.end(), "GSN Strategy"), strategy_names.end());
    EXPECT_NE(std::find(strategy_names.begin(), strategy_names.end(), "SACM ArgumentReasoning"), strategy_names.end());

    parser::SacmElement evidence = MakeElement("E1", "artifactreference", "Evidence", {}, "Test report.");
    std::vector<std::string> evidence_names = ai::SccgAppliesToNamesForElement(evidence);
    EXPECT_NE(std::find(evidence_names.begin(), evidence_names.end(), "GSN Solution"), evidence_names.end());
    EXPECT_NE(std::find(evidence_names.begin(), evidence_names.end(), "SACM ArtifactReference"), evidence_names.end());
}

TEST(AiClaimReviewTest, BuildsPromptWithProvidedClaimGuidelinesAndPayload) {
    ai::AiReviewPayload payload;
    payload.selected = {"selected", "G1", "GSN Goal / SACM Claim", "Goal", "System is safe.", ""};
    payload.children.push_back({"child", "G2", "GSN Goal / SACM Claim", "Sub goal", "Braking is safe.", ""});

    parser::Guideline cl1 = MakeClaimGuideline("CL.1", "Write each claim as a falsifiable proposition");
    parser::ReviewProfile profile = MakeClaimWordingProfile();
    std::vector<const parser::Guideline*> guidelines = {&cl1};

    ai::AiReviewPromptParts parts = ai::BuildAiReviewPrompt(payload, guidelines, &profile);

    EXPECT_NE(parts.prompt.find("Return JSON only"), std::string::npos);
    EXPECT_NE(parts.prompt.find("claim_wording_review"), std::string::npos);
    EXPECT_NE(parts.prompt.find("CL.1"), std::string::npos);
    EXPECT_NE(parts.prompt.find("statement"), std::string::npos);
    EXPECT_NE(parts.prompt.find("rationale"), std::string::npos);
    EXPECT_EQ(parts.prompt.find("SCCG CL rules"), std::string::npos);
    EXPECT_NE(parts.prompt.find("System is safe."), std::string::npos);
    EXPECT_NE(parts.reviewProfileJson.find("claim_wording_review"), std::string::npos);
    EXPECT_NE(parts.debugText.find(parts.prompt), std::string::npos);
    EXPECT_NE(parts.expectedResponseSchema.find("findings"), std::string::npos);
}

TEST(AiClaimReviewTest, PreservesFindingWithResponseGuidelineIdThatWasNotProvided) {
    const std::string response = R"json({
    "reviewed_element_id": "G1",
    "reviewed_element_type": "GSN Goal / SACM Claim",
    "findings": [
        {
            "source": "SCCG",
            "guideline_id": "CL.9",
            "guideline_title": "Unknown",
            "severity": "warning",
            "confidence": "high",
            "message": "Message",
            "why_it_matters": "Why",
            "suggested_fix": "Fix",
            "suggested_claim_wording": "Better wording.",
            "related_element_ids": ["G1"]
        }
    ]
})json";

    ai::AiReviewParseResult parsed = ai::ParseAiReviewResponse(response, "G1", std::vector<std::string>{"CL.1"});

    ASSERT_TRUE(parsed.success) << parsed.errorMessage;
    ASSERT_EQ(parsed.problems.size(), 1u);
    EXPECT_TRUE(parsed.problems[0].guideline_id.empty());
    EXPECT_NE(parsed.problems[0].message.find("Unknown SCCG rule reference: CL.9"), std::string::npos);
}

TEST(AiClaimReviewTest, ParsesFencedJsonAndMapsFindingsToProblems) {
    const std::string response = R"json(```json
{
  "reviewed_element_id": "G1",
  "reviewed_element_type": "GSN Goal / SACM Claim",
  "findings": [
    {
      "source": "SCCG",
      "guideline_id": "CL.2",
      "guideline_title": "Put one main claim in one goal",
      "severity": "unexpected",
      "confidence": "high",
      "message": "The claim bundles multiple conclusions.",
      "why_it_matters": "Bundled claims hide missing support.",
      "suggested_fix": "Split the claim.",
      "suggested_claim_wording": "The braking controller safety is acceptable.",
      "related_element_ids": ["G1"]
    }
  ]
}
```)json";

    ai::ParsedAiReviewResponse parsed = ai::ParseAiReviewResponse(response, "G1", "GSN Goal / SACM Claim");

    ASSERT_TRUE(parsed.success) << parsed.errorMessage;
    ASSERT_EQ(parsed.problems.size(), 1u);
    EXPECT_EQ(parsed.problems[0].id, "ai-review:G1:CL.2:1");
    EXPECT_EQ(parsed.problems[0].source, core::ProblemSource::AIReview);
    EXPECT_EQ(parsed.problems[0].severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(parsed.problems[0].guideline_id, "CL.2");
    EXPECT_NE(parsed.problems[0].message.find("Suggested fix: Split the claim."), std::string::npos);
}

TEST(AiClaimReviewTest, ReportsMissingFindingsArray) {
    ai::ParsedAiReviewResponse parsed =
        ai::ParseAiReviewResponse(R"({"reviewed_element_id":"G1"})", "G1", "GSN Goal / SACM Claim");

    EXPECT_FALSE(parsed.success);
    EXPECT_NE(parsed.errorMessage.find("findings"), std::string::npos);
}
