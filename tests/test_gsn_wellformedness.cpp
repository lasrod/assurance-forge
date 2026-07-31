// GSN Community Standard v3 (SCSC-141C) Core and Dialectic well-formedness.
//
// Test names embed the requirement id from docs/gsn/gsn-v3-conformance-matrix.md
// so a matrix row that claims validation can be traced to the check that backs
// it, and so a check that loses its requirement is visible.

#include "core/problems/gsn_wellformedness.h"

#include "app/structure_problem_sync.h"
#include "core/problems/problems_manager.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

parser::SacmElement Goal(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "claim";
    element.name = element.id;
    return element;
}

parser::SacmElement Strategy(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "argumentreasoning";
    element.name = element.id;
    return element;
}

// A GSN Solution is an ArtifactReference; so is a Context stored as a reference
// to external material. Which one an element is depends on how it is wired.
parser::SacmElement ArtifactReference(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "artifactreference";
    element.name = element.id;
    return element;
}

parser::SacmElement Assumption(std::string id) {
    parser::SacmElement element = Goal(std::move(id));
    element.assertion_declaration = "assumed";
    return element;
}

parser::SacmElement Justification(std::string id) {
    parser::SacmElement element = Goal(std::move(id));
    element.assertion_declaration = "justification";
    return element;
}

// SACM AssertedInference runs premise -> conclusion: the sources support the
// target. GSN's SupportedBy runs the other way; see sacm-gsn-mapping.md.
parser::SacmElement Inference(std::string id,
                              std::vector<std::string> sources,
                              std::string target,
                              std::string reasoning = {}) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "assertedinference";
    element.source_refs = std::move(sources);
    element.target_refs = {std::move(target)};
    element.reasoning_ref = std::move(reasoning);
    return element;
}

parser::SacmElement Evidence(std::string id, std::string source, std::string target) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "assertedevidence";
    element.source_refs = {std::move(source)};
    element.target_refs = {std::move(target)};
    return element;
}

parser::SacmElement Context(std::string id, std::string source, std::string target) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "assertedcontext";
    element.source_refs = {std::move(source)};
    element.target_refs = {std::move(target)};
    return element;
}

parser::SacmElement Counter(parser::SacmElement relationship) {
    relationship.is_counter = true;
    return relationship;
}

// A small argument that violates nothing: G1 is argued by strategy S1 over
// sub-goal G2, G2 is discharged by solution Sn1, and C1 scopes G1.
parser::AssuranceCase WellFormedCase() {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"),
                      Goal("G2"),
                      Strategy("S1"),
                      ArtifactReference("Sn1"),
                      ArtifactReference("C1"),
                      Inference("R1", {"G2"}, "G1", "S1"),
                      Evidence("R2", "Sn1", "G2"),
                      Context("R3", "C1", "G1")};
    return model;
}

std::vector<core::GsnFinding> FindingsOfRule(const parser::AssuranceCase& model, core::GsnRule rule) {
    std::vector<core::GsnFinding> matching;
    for (const core::GsnFinding& finding : core::CheckGsnWellFormedness(model)) {
        if (finding.rule == rule)
            matching.push_back(finding);
    }
    return matching;
}

bool HasProblemOfType(const core::ProblemsManager& problems, const std::string& type) {
    const std::vector<core::ProblemItem>& items = problems.GetProblems();
    return std::any_of(items.begin(), items.end(), [&](const core::ProblemItem& problem) {
        return problem.type == type;
    });
}

} // namespace

// ===== The checker stays quiet on a valid argument =====

TEST(GsnWellFormednessTest, AValidCoreArgumentProducesNoFindings) {
    EXPECT_TRUE(core::CheckGsnWellFormedness(WellFormedCase()).empty());
}

TEST(GsnWellFormednessTest, AnEmptyCaseProducesNoFindings) {
    EXPECT_TRUE(core::CheckGsnWellFormedness(parser::AssuranceCase{}).empty());
}

// A file the tool did not author will contain constructs it has no mapping for.
// Guessing at their GSN role would report a foreign but valid argument as
// broken, so unrecognized content is passed over rather than judged.
TEST(GsnWellFormednessTest, UnrecognizedElementTypesAreNotJudged) {
    parser::AssuranceCase model;
    parser::SacmElement foreign;
    foreign.id = "X1";
    foreign.type = "choice"; // GSN v2.2 Choice: preserved, no SACM 2.3 equivalent
    model.elements = {Goal("G1"), foreign, Inference("R1", {"X1"}, "G1"), Inference("R2", {"G1"}, "X1")};

    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
}

// ===== GSN3-CORE-015: only permitted element/relationship combinations =====

TEST(GsnWellFormednessTest, GSN3_CORE_015_ASolutionCannotBeSupported) {
    parser::AssuranceCase model = WellFormedCase();
    // Sn1 is a Solution, and a Solution is a leaf: nothing is argued beneath it.
    model.elements.push_back(Goal("G3"));
    model.elements.push_back(Inference("R4", {"G3"}, "Sn1"));

    const std::vector<core::GsnFinding> findings =
        FindingsOfRule(model, core::GsnRule::SupportedElementIsALeaf);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().element_id, "Sn1");
    EXPECT_EQ(findings.front().relationship_id, "R4");
    EXPECT_STREQ(core::GsnRequirementId(findings.front().rule), "GSN3-CORE-015");
}

TEST(GsnWellFormednessTest, GSN3_CORE_015_AnAssumptionCannotBeSupported) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Assumption("A1"), Goal("G2"), Context("R1", "A1", "G1"),
                      Inference("R2", {"G2"}, "A1")};

    const std::vector<core::GsnFinding> findings =
        FindingsOfRule(model, core::GsnRule::SupportedElementIsALeaf);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().element_id, "A1");
}

TEST(GsnWellFormednessTest, GSN3_CORE_015_AJustificationCannotCarryContext) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Justification("J1"), ArtifactReference("C1"), Context("R1", "J1", "G1"),
                      Context("R2", "C1", "J1")};

    const std::vector<core::GsnFinding> findings =
        FindingsOfRule(model, core::GsnRule::ContextualizedElementIsALeaf);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().element_id, "J1");
    EXPECT_EQ(findings.front().relationship_id, "R2");
}

// A Strategy is a legitimate InContextOf subject: "argue over each hazard"
// needs the Context that fixes the set of hazards.
TEST(GsnWellFormednessTest, GSN3_CORE_015_AStrategyMayCarryContext) {
    parser::AssuranceCase model = WellFormedCase();
    model.elements.push_back(ArtifactReference("C2"));
    model.elements.push_back(Context("R4", "C2", "S1"));

    EXPECT_TRUE(FindingsOfRule(model, core::GsnRule::ContextualizedElementIsALeaf).empty());
}

// ===== GSN3-CORE-002: the Strategy's role in the inference =====

TEST(GsnWellFormednessTest, GSN3_CORE_002_AStrategyWiredAsAnInferenceEndIsReported) {
    parser::AssuranceCase model;
    // The strategy belongs in the inference's `reasoning` slot. Wired as a
    // source it is also invalid SACM: clause 11.13 requires an Assertion there.
    model.elements = {Goal("G1"), Strategy("S1"), Inference("R1", {"S1"}, "G1")};

    const std::vector<core::GsnFinding> findings =
        FindingsOfRule(model, core::GsnRule::StrategyUsedAsAssertion);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().element_id, "S1");
    EXPECT_STREQ(core::GsnRequirementId(findings.front().rule), "GSN3-CORE-002");
}

TEST(GsnWellFormednessTest, GSN3_CORE_002_AStrategyInTheReasoningSlotIsCorrect) {
    EXPECT_TRUE(FindingsOfRule(WellFormedCase(), core::GsnRule::StrategyUsedAsAssertion).empty());
}

// ===== GSN3-CORE-003: a Solution references evidence =====

TEST(GsnWellFormednessTest, GSN3_CORE_003_AGoalCannotStandInForEvidence) {
    parser::AssuranceCase model;
    // A Goal in the evidence position makes the argument look discharged by an
    // artifact that does not exist.
    model.elements = {Goal("G1"), Goal("G2"), Evidence("R1", "G2", "G1")};

    const std::vector<core::GsnFinding> findings =
        FindingsOfRule(model, core::GsnRule::EvidenceSourceIsNotASolution);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().element_id, "G2");
    EXPECT_STREQ(core::GsnRequirementId(findings.front().rule), "GSN3-CORE-003");
}

// ===== GSN3-CORE-007: relationship endpoints resolve =====

TEST(GsnWellFormednessTest, GSN3_CORE_007_AnEndpointNamingAMissingElementIsReported) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Inference("R1", {"G-missing"}, "G1")};

    const std::vector<core::GsnFinding> findings = FindingsOfRule(model, core::GsnRule::UnresolvedEndpoint);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().detail, "G-missing");
    EXPECT_EQ(findings.front().relationship_id, "R1");
    // Anchored on the end that still resolves, so the reader lands next to the
    // damage rather than nowhere.
    EXPECT_EQ(findings.front().element_id, "G1");
}

// References appear as bare ids, as `#id`, and against an element's gid. A
// lookup that only matched `id` would report half of a real file as broken.
TEST(GsnWellFormednessTest, GSN3_CORE_007_HashPrefixedAndGidReferencesResolve) {
    parser::AssuranceCase model;
    parser::SacmElement sub_goal = Goal("G2");
    sub_goal.gid = "urn:af:G2";
    model.elements = {Goal("G1"), sub_goal, Inference("R1", {"urn:af:G2"}, "#G1")};

    EXPECT_TRUE(FindingsOfRule(model, core::GsnRule::UnresolvedEndpoint).empty());
}

TEST(GsnWellFormednessTest, GSN3_CORE_007_AMissingReasoningReferenceIsReported) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), Inference("R1", {"G2"}, "G1", "S-missing")};

    const std::vector<core::GsnFinding> findings = FindingsOfRule(model, core::GsnRule::UnresolvedEndpoint);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().detail, "S-missing");
}

// ===== GSN3-CORE-009: the undeveloped decorator =====

TEST(GsnWellFormednessTest, GSN3_CORE_009_AnUndevelopedElementWithSupportIsReported) {
    parser::AssuranceCase model;
    parser::SacmElement top = Goal("G1");
    top.undeveloped = true;
    model.elements = {top, Goal("G2"), Inference("R1", {"G2"}, "G1")};

    const std::vector<core::GsnFinding> findings =
        FindingsOfRule(model, core::GsnRule::UndevelopedElementHasSupport);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().element_id, "G1");
    EXPECT_STREQ(core::GsnRequirementId(findings.front().rule), "GSN3-CORE-009");
}

// An undeveloped goal with nothing under it is the decorator doing its job: an
// honest statement of incompleteness, not a defect.
TEST(GsnWellFormednessTest, GSN3_CORE_009_AnUndevelopedLeafIsNotAProblem) {
    parser::AssuranceCase model;
    parser::SacmElement leaf = Goal("G2");
    leaf.undeveloped = true;
    model.elements = {Goal("G1"), leaf, Inference("R1", {"G2"}, "G1")};

    EXPECT_TRUE(FindingsOfRule(model, core::GsnRule::UndevelopedElementHasSupport).empty());
}

// A challenge is not support: counter-evidence attacking an undeveloped goal
// does not develop it.
TEST(GsnWellFormednessTest, GSN3_CORE_009_AChallengeIsNotSupport) {
    parser::AssuranceCase model;
    parser::SacmElement top = Goal("G1");
    top.undeveloped = true;
    model.elements = {top, Goal("CG1"), Counter(Inference("R1", {"CG1"}, "G1"))};

    EXPECT_TRUE(FindingsOfRule(model, core::GsnRule::UndevelopedElementHasSupport).empty());
}

// ===== GSN3-CORE-010: one identifier per element, unique =====

TEST(GsnWellFormednessTest, GSN3_CORE_010_TwoElementsSharingANotationIdentifierAreReported) {
    parser::AssuranceCase model;
    parser::SacmElement renumbered = Goal("G2");
    // An imported or renumbered node can collide with another node's identifier
    // even though their SACM storage ids differ.
    renumbered.gsn_identifier = "G1";
    model.elements = {Goal("G1"), renumbered};

    const std::vector<core::GsnFinding> findings =
        FindingsOfRule(model, core::GsnRule::DuplicateNotationIdentifier);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().element_id, "G2");
    EXPECT_EQ(findings.front().related_id, "G1");
    EXPECT_EQ(findings.front().detail, "G1");
    EXPECT_STREQ(core::GsnRequirementId(findings.front().rule), "GSN3-CORE-010");
}

TEST(GsnWellFormednessTest, GSN3_CORE_010_DistinctIdentifiersAreAccepted) {
    EXPECT_TRUE(FindingsOfRule(WellFormedCase(), core::GsnRule::DuplicateNotationIdentifier).empty());
}

// ===== GSN3-DIA-003: a challenge names what it challenges =====

TEST(GsnWellFormednessTest, GSN3_DIA_003_AChallengeWithAMissingTargetIsReported) {
    parser::AssuranceCase model;
    model.elements = {Goal("CG1"), Counter(Inference("R1", {"CG1"}, "G-missing"))};

    const std::vector<core::GsnFinding> findings =
        FindingsOfRule(model, core::GsnRule::ChallengeTargetUnresolved);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().detail, "G-missing");
    EXPECT_STREQ(core::GsnRequirementId(findings.front().rule), "GSN3-DIA-003");
}

// GSN v3 lets a challenge point at an element or at a relationship, and the
// counter-evidence that carries it is not evidence *for* anything. Judging a
// challenge by the support rules would report every dialectic argument as
// malformed — the failure that already produced AF-ENG-015 in the SVG export.
TEST(GsnWellFormednessTest, GSN3_DIA_003_AWellFormedChallengeProducesNoFindings) {
    parser::AssuranceCase model = WellFormedCase();
    model.elements.push_back(Goal("CG1"));
    model.elements.push_back(ArtifactReference("CSn1"));
    model.elements.push_back(Counter(Inference("R4", {"CG1"}, "G2")));
    // A challenge to a relationship, and a challenge to that challenge.
    model.elements.push_back(Counter(Evidence("R5", "CSn1", "R1")));
    model.elements.push_back(Counter(Inference("R6", {"CG1"}, "R5")));

    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
}

// ===== Determinism =====

// Two files can serialize the same argument in different orders. A finding list
// that depended on that order would make a diff of the problems panel useless.
TEST(GsnWellFormednessTest, FindingsDoNotDependOnDocumentOrder) {
    parser::AssuranceCase forward;
    forward.elements = {Goal("G1"), Goal("G2"), ArtifactReference("Sn1"), Inference("R1", {"G2"}, "Sn1"),
                        Evidence("R2", "G2", "G1")};

    parser::AssuranceCase reversed;
    reversed.elements = forward.elements;
    std::reverse(reversed.elements.begin(), reversed.elements.end());

    const std::vector<core::GsnFinding> a = core::CheckGsnWellFormedness(forward);
    const std::vector<core::GsnFinding> b = core::CheckGsnWellFormedness(reversed);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_FALSE(a.empty());
    for (size_t index = 0; index < a.size(); ++index) {
        EXPECT_EQ(a[index].rule, b[index].rule);
        EXPECT_EQ(a[index].element_id, b[index].element_id);
        EXPECT_EQ(a[index].relationship_id, b[index].relationship_id);
    }
}

// ===== The app-level sync: detection only counts if a user sees it =====

TEST(GsnWellFormednessSyncTest, ReportsAViolationWithItsGsnRequirementId) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), ArtifactReference("Sn1"), Evidence("R1", "Sn1", "G1"),
                      Inference("R2", {"G2"}, "Sn1")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);

    const std::vector<core::ProblemItem>& items = problems.GetProblems();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front().type, "SupportedElementIsALeaf");
    EXPECT_EQ(items.front().severity, core::ProblemSeverity::Error);
    EXPECT_EQ(items.front().source, core::ProblemSource::ModelValidation);
    EXPECT_EQ(items.front().element_id, "Sn1");
    // The requirement id is what lets a reader check the tool against the
    // standard instead of trusting it.
    EXPECT_EQ(items.front().guideline_id, "GSN3-CORE-015");
    EXPECT_NE(items.front().message.find("Sn1"), std::string::npos);
}

// A stale decorator leaves a valid argument; a broken connection does not.
TEST(GsnWellFormednessSyncTest, AStaleUndevelopedDecoratorIsAWarningNotAnError) {
    parser::AssuranceCase model;
    parser::SacmElement top = Goal("G1");
    top.undeveloped = true;
    model.elements = {top, Goal("G2"), Inference("R1", {"G2"}, "G1")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);

    ASSERT_EQ(problems.GetProblems().size(), 1u);
    EXPECT_EQ(problems.GetProblems().front().severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(problems.GetProblems().front().guideline_id, "GSN3-CORE-009");
}

TEST(GsnWellFormednessSyncTest, ClearsTheProblemWhenTheStructureIsCorrected) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), ArtifactReference("Sn1"), Evidence("R1", "Sn1", "G1"),
                      Inference("R2", {"G2"}, "Sn1")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);
    ASSERT_TRUE(HasProblemOfType(problems, "SupportedElementIsALeaf"));

    model.elements.pop_back(); // re-parent the sub-goal off the solution
    app::SyncStructureProblems(problems, &model);
    EXPECT_FALSE(HasProblemOfType(problems, "SupportedElementIsALeaf"));
}

TEST(GsnWellFormednessSyncTest, AValidArgumentRaisesNoProblems) {
    parser::AssuranceCase model = WellFormedCase();

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);

    EXPECT_TRUE(problems.GetProblems().empty());
}
