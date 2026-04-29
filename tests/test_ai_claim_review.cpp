#include <gtest/gtest.h>

#include "ai/ai_claim_review.h"
#include "core/assurance_tree.h"

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
    guideline.guideline = "Write a clear claim.";
    guideline.why = "Reviewers need clear claims.";
    guideline.review_prompts = {"Is the claim reviewable?"};
    return guideline;
}

}  // namespace

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

TEST(AiClaimReviewTest, RejectsNonClaimElements) {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("S1", "argumentreasoning", "Strategy", "By decomposition."));
    core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    ai::AiReviewPayload payload;
    std::string error;

    EXPECT_FALSE(ai::BuildAiReviewPayload(assurance_case, tree, "S1", payload, error));
    EXPECT_EQ(error, "AI Review currently supports GSN Goal / SACM Claim elements only.");
}

TEST(AiClaimReviewTest, BuildsPromptWithProvidedClaimGuidelinesAndPayload) {
    ai::AiReviewPayload payload;
    payload.selected = {"selected", "G1", "GSN Goal / SACM Claim", "Goal", "System is safe.", ""};
    payload.children.push_back({"child", "G2", "GSN Goal / SACM Claim", "Sub goal", "Braking is safe.", ""});

    parser::Guideline cl1 = MakeClaimGuideline("CL.1", "Write each claim as a falsifiable proposition");
    std::vector<const parser::Guideline*> guidelines = {&cl1};

    ai::AiReviewPromptParts parts = ai::BuildAiReviewPrompt(payload, guidelines);

    EXPECT_NE(parts.prompt.find("Return JSON only"), std::string::npos);
    EXPECT_NE(parts.prompt.find("CL.1"), std::string::npos);
    EXPECT_NE(parts.prompt.find("System is safe."), std::string::npos);
    EXPECT_NE(parts.debugText.find(parts.prompt), std::string::npos);
    EXPECT_NE(parts.expectedResponseSchema.find("findings"), std::string::npos);
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
    EXPECT_EQ(parsed.problems[0].id, "ai-review:G1:CL.2");
    EXPECT_EQ(parsed.problems[0].source, core::ProblemSource::AIReview);
    EXPECT_EQ(parsed.problems[0].severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(parsed.problems[0].guideline_id, "CL.2");
    EXPECT_NE(parsed.problems[0].message.find("Suggested fix: Split the claim."), std::string::npos);
}

TEST(AiClaimReviewTest, ReportsMissingFindingsArray) {
    ai::ParsedAiReviewResponse parsed = ai::ParseAiReviewResponse(R"({"reviewed_element_id":"G1"})",
                                                                  "G1",
                                                                  "GSN Goal / SACM Claim");

    EXPECT_FALSE(parsed.success);
    EXPECT_NE(parsed.errorMessage.find("findings"), std::string::npos);
}
