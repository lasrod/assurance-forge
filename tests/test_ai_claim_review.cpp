#include "review/sccg/sccg_review.h"

#include "core/assurance_tree.h"
#include "core/reviews/review_proposal.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
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

bool HasPackage(const review::AiReviewDataPackageBundle& packages, const std::string& id) {
    return std::find_if(packages.available.begin(),
                        packages.available.end(),
                        [&](const review::AiReviewDataPackage& package) { return package.id == id; }) !=
           packages.available.end();
}

bool HasUnavailablePackage(const review::AiReviewDataPackageBundle& packages, const std::string& id) {
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
    review::AiReviewPayload payload;
    std::string error;

    ASSERT_TRUE(review::BuildAiReviewPayload(assurance_case, tree, "G1", payload, error)) << error;
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

    review::AiReviewDataPackageBundle packages;
    std::string error;
    ASSERT_TRUE(review::CollectAiReviewDataPackages(assurance_case, tree, "G2", &profile, packages, error)) << error;

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
    review::AiReviewPayload payload;
    std::string error;

    EXPECT_FALSE(review::BuildAiReviewPayload(assurance_case, tree, "A1", payload, error));
    EXPECT_EQ(error, "AI Review does not support the selected element type.");
}

TEST(AiClaimReviewTest, MapsStrategyAndEvidenceToSccgAppliesToNames) {
    parser::SacmElement strategy = MakeElement("S1", "argumentreasoning", "Strategy", "By decomposition.");
    std::vector<std::string> strategy_names = review::SccgAppliesToNamesForElement(strategy);
    EXPECT_NE(std::find(strategy_names.begin(), strategy_names.end(), "GSN Strategy"), strategy_names.end());
    EXPECT_NE(std::find(strategy_names.begin(), strategy_names.end(), "SACM ArgumentReasoning"), strategy_names.end());

    parser::SacmElement evidence = MakeElement("E1", "artifactreference", "Evidence", {}, "Test report.");
    std::vector<std::string> evidence_names = review::SccgAppliesToNamesForElement(evidence);
    EXPECT_NE(std::find(evidence_names.begin(), evidence_names.end(), "GSN Solution"), evidence_names.end());
    EXPECT_NE(std::find(evidence_names.begin(), evidence_names.end(), "SACM ArtifactReference"), evidence_names.end());
}

TEST(AiClaimReviewTest, BuildsPromptWithProvidedClaimGuidelinesAndPayload) {
    review::AiReviewPayload payload;
    payload.selected = {"selected", "G1", "GSN Goal / SACM Claim", "Goal", "System is safe.", ""};
    payload.children.push_back({"child", "G2", "GSN Goal / SACM Claim", "Sub goal", "Braking is safe.", ""});

    parser::Guideline cl1 = MakeClaimGuideline("CL.1", "Write each claim as a falsifiable proposition");
    parser::ReviewProfile profile = MakeClaimWordingProfile();
    std::vector<const parser::Guideline*> guidelines = {&cl1};

    review::AiReviewPromptParts parts = review::BuildAiReviewPrompt(payload, guidelines, &profile);

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

    review::AiReviewParseResult parsed =
        review::ParseAiReviewResponse(response, "G1", std::vector<std::string>{"CL.1"});

    ASSERT_TRUE(parsed.errorMessage.empty()) << parsed.errorMessage;
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

    review::ParsedAiReviewResponse parsed = review::ParseAiReviewResponse(response, "G1", "GSN Goal / SACM Claim");

    ASSERT_TRUE(parsed.errorMessage.empty()) << parsed.errorMessage;
    ASSERT_EQ(parsed.problems.size(), 1u);
    ASSERT_EQ(parsed.suggestedElementTexts.size(), 1u);
    EXPECT_EQ(parsed.problems[0].id, "ai-review:G1:CL.2:1");
    EXPECT_EQ(parsed.problems[0].source, core::ProblemSource::AIReview);
    EXPECT_EQ(parsed.problems[0].severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(parsed.problems[0].guideline_id, "CL.2");
    EXPECT_EQ(parsed.suggestedElementTexts[0], "The braking controller safety is acceptable.");
    EXPECT_NE(parsed.problems[0].message.find("Suggested fix:\nSplit the claim."), std::string::npos);
}

TEST(AiClaimReviewTest, ParsesGenericSuggestedElementText) {
    const std::string response = R"json({
    "reviewed_element_id": "S1",
    "reviewed_element_type": "GSN Strategy / SACM ArgumentReasoning",
    "findings": [
        {
            "source": "SCCG",
            "guideline_id": "AR.2",
            "guideline_title": "State the inference step explicitly",
            "severity": "warning",
            "confidence": "high",
            "message": "The strategy does not explain the decomposition rule.",
            "why_it_matters": "Reviewers need to understand why the children support the parent.",
            "suggested_fix": "State the decomposition rule explicitly.",
            "suggested_element_text": "Argument by credible hazard class, covering blade contact, electrical, thermal, mechanical, residual-risk, and production-control hazards.",
            "related_element_ids": ["S1"]
        }
    ]
})json";

    review::ParsedAiReviewResponse parsed =
        review::ParseAiReviewResponse(response, "S1", "GSN Strategy / SACM ArgumentReasoning");

    ASSERT_TRUE(parsed.errorMessage.empty()) << parsed.errorMessage;
    ASSERT_EQ(parsed.suggestedElementTexts.size(), 1u);
    EXPECT_EQ(parsed.suggestedElementTexts[0],
              "Argument by credible hazard class, covering blade contact, electrical, thermal, mechanical, "
              "residual-risk, and production-control hazards.");
    ASSERT_EQ(parsed.problems.size(), 1u);
    EXPECT_NE(parsed.problems[0].message.find("Suggested element text"), std::string::npos);
}

TEST(AiClaimReviewTest, ReportsMissingFindingsArray) {
    review::ParsedAiReviewResponse parsed =
        review::ParseAiReviewResponse(R"({"reviewed_element_id":"G1"})", "G1", "GSN Goal / SACM Claim");

    EXPECT_FALSE(parsed.errorMessage.empty());
    EXPECT_NE(parsed.errorMessage.find("findings"), std::string::npos);
}

// A repair the parser cannot read is reported, not dropped: a finding whose
// operations silently vanished reads as a finding nobody could fix.
TEST(AiClaimReviewTest, ReportsAProposedOperationItCannotRead) {
    const std::string response = R"json({
      "reviewed_element_id": "G1",
      "findings": [
        {
          "source": "SCCG",
          "guideline_id": "AR.2",
          "severity": "warning",
          "message": "No reasoning step.",
          "proposed_operations": [{"type": "NotARealOperation"}]
        }
      ]
    })json";

    const review::AiReviewParseResult parsed = review::ParseAiReviewResponse(response, "G1");

    EXPECT_TRUE(parsed.errorMessage.empty());
    ASSERT_EQ(parsed.problems.size(), 1u);
    ASSERT_EQ(parsed.proposedOperations.size(), 1u);
    EXPECT_TRUE(parsed.proposedOperations[0].empty());
    ASSERT_EQ(parsed.rejectedOperationReasons.size(), 1u);
    EXPECT_NE(parsed.rejectedOperationReasons[0].find("NotARealOperation"), std::string::npos);
}

// The structural repair SCCG describes for AR.2, read off the wire.
TEST(AiClaimReviewTest, ParsesAStructuralRepairFromAFinding) {
    const std::string response = R"json({
      "reviewed_element_id": "G1",
      "findings": [
        {
          "source": "SCCG",
          "guideline_id": "AR.2",
          "severity": "warning",
          "message": "This decomposition states no reasoning step.",
          "proposed_operations": [
            {"type": "CreateStrategy", "create_ref": "$strategy", "text": "Argue over hazard classes"},
            {"type": "AddSupportedBy", "source": {"ref": "$strategy"}, "target": {"id": "G1"}}
          ]
        }
      ]
    })json";

    const review::AiReviewParseResult parsed = review::ParseAiReviewResponse(response, "G1");

    EXPECT_TRUE(parsed.rejectedOperationReasons.empty());
    ASSERT_EQ(parsed.proposedOperations.size(), 1u);
    ASSERT_EQ(parsed.proposedOperations[0].size(), 2u);
    EXPECT_EQ(parsed.proposedOperations[0][0].type, core::reviews::PatchOperationType::CreateStrategy);
    ASSERT_TRUE(parsed.proposedOperations[0][0].create_ref.has_value());
    EXPECT_EQ(parsed.proposedOperations[0][0].create_ref.value(), "$strategy");
    // The ref form the schema advertises has to survive the parse, or the
    // attachment would name nothing.
    ASSERT_TRUE(parsed.proposedOperations[0][1].source.has_value());
    ASSERT_TRUE(parsed.proposedOperations[0][1].source->create_ref.has_value());
    EXPECT_EQ(parsed.proposedOperations[0][1].source->create_ref.value(), "$strategy");
    ASSERT_TRUE(parsed.proposedOperations[0][1].target.has_value());
    ASSERT_TRUE(parsed.proposedOperations[0][1].target->existing_id.has_value());
    EXPECT_EQ(parsed.proposedOperations[0][1].target->existing_id.value(), "G1");
}

// The parser fills an object the caller owns, so a second parse must not leave
// the first one's refs behind.
TEST(AiClaimReviewTest, ClearsRefsCarriedOverFromAnEarlierParse) {
    core::reviews::PatchOperation operation;
    std::string error;
    ASSERT_TRUE(core::reviews::ParsePatchOperationJson(
        nlohmann::json::parse(R"json({"type": "AddSupportedBy", "source": {"id": "G2"}})json"), operation, error))
        << error;
    ASSERT_TRUE(operation.source.has_value());

    ASSERT_TRUE(core::reviews::ParsePatchOperationJson(
        nlohmann::json::parse(R"json({"type": "SetUndeveloped", "element": {"id": "G1"}})json"), operation, error))
        << error;
    EXPECT_FALSE(operation.source.has_value()) << "a ref survived from the previous operation";
}

// --- S4: data packages the tool can now actually supply -------------------

namespace {

// Takes an lvalue only: returning a pointer into a temporary bundle compiled
// fine and passed in Release, where the freed memory happened to still hold the
// value. Debug poisons it, which is how CI found what the local run could not.
const review::AiReviewUnavailableDataPackage* Unavailable(const review::AiReviewDataPackageBundle&& packages,
                                                          const std::string& id) = delete;

const review::AiReviewUnavailableDataPackage* Unavailable(const review::AiReviewDataPackageBundle& packages,
                                                          const std::string& id) {
    for (const review::AiReviewUnavailableDataPackage& package : packages.unavailable) {
        if (package.id == id)
            return &package;
    }
    return nullptr;
}

review::AiReviewDataPackageBundle CollectFor(const parser::AssuranceCase& assurance_case,
                                             const std::string& element_id,
                                             const review::AiReviewCaseContext* context) {
    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    review::AiReviewDataPackageBundle packages;
    std::string error;
    EXPECT_TRUE(
        review::CollectAiReviewDataPackages(assurance_case, tree, element_id, nullptr, packages, error, context))
        << error;
    return packages;
}

parser::AssuranceCase CaseWithATerm() {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("G1", "claim", "Top goal", "The vehicle is safe."));
    parser::SacmElement term;
    term.id = "T1";
    term.type = "term";
    term.name = "safe";
    term.content = "safe";
    term.description = "No unreasonable risk of harm within the stated ODD.";
    assurance_case.elements.push_back(std::move(term));
    return assurance_case;
}

} // namespace

// CL.4 and AR.6 ask whether a broad word is bounded somewhere. The case has
// been able to hold that definition since terminology landed; reviewing without
// it asked the model to judge ambiguity against definitions the project had
// already written.
TEST(AiClaimReviewTest, SuppliesTheProjectGlossaryFromTheCasesOwnTerms) {
    const review::AiReviewDataPackageBundle packages = CollectFor(CaseWithATerm(), "G1", nullptr);

    ASSERT_TRUE(HasPackage(packages, "PROJECT_GLOSSARY"));
    for (const review::AiReviewDataPackage& package : packages.available) {
        if (package.id != "PROJECT_GLOSSARY")
            continue;
        EXPECT_NE(package.json.find("No unreasonable risk of harm"), std::string::npos);
        EXPECT_NE(package.json.find("\"term\""), std::string::npos);
    }
}

TEST(AiClaimReviewTest, ReportsAnEmptyGlossaryAsEmptyRatherThanUnimplemented) {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("G1", "claim", "Top goal", "The vehicle is safe."));

    const review::AiReviewDataPackageBundle packages = CollectFor(assurance_case, "G1", nullptr);
    const review::AiReviewUnavailableDataPackage* glossary = Unavailable(packages, "PROJECT_GLOSSARY");
    ASSERT_NE(glossary, nullptr);
    EXPECT_EQ(glossary->absence, review::DataPackageAbsence::Empty);
}

// SU.4, SU.5 and SU.11 all turn on whether this claim has been challenged
// before, and how that challenge ended.
TEST(AiClaimReviewTest, SuppliesPriorFindingsAsChangeHistory) {
    review::AiReviewCaseContext context;
    core::reviews::ReviewItem item;
    item.id = "item-1";
    item.element_id = "G1";
    item.title = "CL.5 unbounded qualifier";
    item.message = "\"safe\" is not bounded here.";
    item.severity = "warning";
    item.reviewer_name = "A human";
    item.guideline_ids = {"CL.5"};
    context.review_items.push_back(std::move(item));

    const review::AiReviewDataPackageBundle packages = CollectFor(CaseWithATerm(), "G1", &context);

    ASSERT_TRUE(HasPackage(packages, "CHANGE_HISTORY"));
    for (const review::AiReviewDataPackage& package : packages.available) {
        if (package.id == "CHANGE_HISTORY")
            EXPECT_NE(package.json.find("CL.5 unbounded qualifier"), std::string::npos);
    }
}

// A finding about a neighbour is not this review's history.
TEST(AiClaimReviewTest, LeavesOutHistoryForElementsOutsideTheReview) {
    review::AiReviewCaseContext context;
    core::reviews::ReviewItem elsewhere;
    elsewhere.id = "item-2";
    elsewhere.element_id = "SOMEWHERE_ELSE";
    elsewhere.title = "A finding on another branch";
    context.review_items.push_back(std::move(elsewhere));

    const review::AiReviewDataPackageBundle packages = CollectFor(CaseWithATerm(), "G1", &context);
    const review::AiReviewUnavailableDataPackage* history = Unavailable(packages, "CHANGE_HISTORY");
    ASSERT_NE(history, nullptr);
    EXPECT_EQ(history->absence, review::DataPackageAbsence::Empty);
}

TEST(AiClaimReviewTest, CarriesTheUsersOwnConcernWhenTheyStatedOne) {
    review::AiReviewCaseContext context;
    context.user_review_intent = "I am worried the ODD bound is not stated.";

    EXPECT_TRUE(HasPackage(CollectFor(CaseWithATerm(), "G1", &context), "USER_REVIEW_INTENT"));
}

// The distinction the evidence-register decision needs: a package the tool has
// no source for reads differently from one the case simply has none of, and
// both read differently from one deliberately not shared.
TEST(AiClaimReviewTest, NamesWhyEachAbsentPackageIsAbsent) {
    const review::AiReviewDataPackageBundle packages = CollectFor(CaseWithATerm(), "G1", nullptr);

    const review::AiReviewUnavailableDataPackage* basis = Unavailable(packages, "EVIDENCE_BASIS");
    ASSERT_NE(basis, nullptr);
    EXPECT_EQ(basis->absence, review::DataPackageAbsence::NotImplemented);
    EXPECT_NE(basis->reason.find("evidence register"), std::string::npos)
        << "the recorded route should survive in the reason a reviewer reads";

    const review::AiReviewUnavailableDataPackage* links = Unavailable(packages, "STANDARD_LINKS");
    ASSERT_NE(links, nullptr);
    EXPECT_EQ(links->absence, review::DataPackageAbsence::NotImplemented);
}

// A challenge standing against the parent claim is part of this review's
// history: the parent is in the request, so its findings belong with it. The
// collector scopes by what the packages carry, and a caller that pre-filtered
// to the selected element would hide exactly the findings SU.11 is about.
TEST(AiClaimReviewTest, IncludesHistoryForInScopeElementsBeyondTheSelectedOne) {
    parser::AssuranceCase assurance_case;
    assurance_case.elements.push_back(MakeElement("G1", "claim", "Top goal", "System is safe."));
    assurance_case.elements.push_back(MakeElement("G2", "claim", "Sub goal", "Braking is safe."));
    assurance_case.elements.push_back(MakeRelationship("INF1", "assertedinference", {"G2"}, {"G1"}));

    review::AiReviewCaseContext context;
    core::reviews::ReviewItem on_parent;
    on_parent.id = "item-parent";
    on_parent.element_id = "G1";
    on_parent.title = "SU.11 challenge left unresolved on the parent";
    on_parent.guideline_ids = {"SU.11"};
    context.review_items.push_back(std::move(on_parent));

    const core::AssuranceTree tree = core::AssuranceTree::Build(assurance_case);
    review::AiReviewDataPackageBundle packages;
    std::string error;
    ASSERT_TRUE(review::CollectAiReviewDataPackages(assurance_case, tree, "G2", nullptr, packages, error, &context))
        << error;

    ASSERT_TRUE(HasPackage(packages, "CHANGE_HISTORY"));
    for (const review::AiReviewDataPackage& package : packages.available) {
        if (package.id == "CHANGE_HISTORY")
            EXPECT_NE(package.json.find("SU.11 challenge left unresolved"), std::string::npos) << package.json;
    }
}
