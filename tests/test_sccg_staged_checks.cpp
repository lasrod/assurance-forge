#include "core/sccg/staged_checks.h"

#include "core/guideline_catalog.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// The mechanically-decidable subset of SCCG, run against what an agent staged.
//
// Each test names the guideline it covers. The set is small on purpose: most of
// SCCG is prose only a reader can judge, and a check that claimed otherwise
// would make a green result read as conformance when it is not.

namespace {

parser::SacmElement Claim(const std::string& id, const std::string& text, bool undeveloped = false) {
    parser::SacmElement element;
    element.id = id;
    element.type = "claim";
    element.name = id;
    element.content = text;
    element.undeveloped = undeveloped;
    return element;
}

parser::SacmElement Strategy(const std::string& id, const std::string& text) {
    parser::SacmElement element;
    element.id = id;
    element.type = "argumentreasoning";
    element.name = id;
    element.content = text;
    return element;
}

parser::SacmElement Solution(const std::string& id, const std::string& text = std::string()) {
    parser::SacmElement element;
    element.id = id;
    element.type = "artifactreference";
    element.name = id;
    element.content = text;
    return element;
}

// An AssertedInference: SACM's source is the premise and its target the
// conclusion, so support runs from the child up to the parent.
parser::SacmElement Supports(const std::string& id, const std::string& child, const std::string& parent) {
    parser::SacmElement element;
    element.id = id;
    element.type = "assertedinference";
    element.source_refs = {child};
    element.target_refs = {parent};
    return element;
}

// An AssertedEvidence: this is what gives an artifact the Solution role, and it
// is the only relationship that does. Using an inference here would leave the
// artifact a generic node and the test would pass for the wrong reason.
parser::SacmElement Evidences(const std::string& id, const std::string& artifact, const std::string& claim) {
    parser::SacmElement element;
    element.id = id;
    element.type = "assertedevidence";
    element.source_refs = {artifact};
    element.target_refs = {claim};
    return element;
}

bool Mentions(const std::vector<core::sccg::StagedFinding>& findings,
              const std::string& guideline_id,
              const std::string& element_id) {
    for (const core::sccg::StagedFinding& finding : findings) {
        if (finding.guideline_id == guideline_id && finding.element_id == element_id) {
            return true;
        }
    }
    return false;
}

const core::sccg::StagedFinding* FindByCheck(const std::vector<core::sccg::StagedFinding>& findings,
                                             const std::string& check_id,
                                             const std::string& element_id) {
    for (const core::sccg::StagedFinding& finding : findings) {
        if (finding.check_id == check_id && finding.element_id == element_id) {
            return &finding;
        }
    }
    return nullptr;
}

// One claim, checked. The text is the whole model, which is exactly the shape
// of the lexical checks: they judge sentences, not structure.
std::vector<core::sccg::StagedFinding> CheckClaimText(const std::string& text) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", text, /*undeveloped=*/true));
    return core::sccg::CheckStagedArgument(model, {"G1"});
}

// One solution with the given text, evidencing a claim so it holds the
// Solution role the evidence checks apply to.
std::vector<core::sccg::StagedFinding> CheckSolutionText(const std::string& text) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Top goal", /*undeveloped=*/true));
    model.elements.push_back(Solution("Sn1", text));
    model.elements.push_back(Evidences("R1", "Sn1", "G1"));
    return core::sccg::CheckStagedArgument(model, {"Sn1"});
}

} // namespace

// EV.1 -- show the evidence path for every claim. A claim with nothing under it
// and no undeveloped marker leaves a reviewer unable to tell a gap from work
// still to come.
TEST(SccgStagedChecks, EV1_FlagsAClaimWithNoSupportAndNoUndevelopedMarker) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The braking function meets its requirements"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    EXPECT_TRUE(Mentions(findings, "EV.1", "G1"));
}

TEST(SccgStagedChecks, EV1_AcceptsAClaimDeliberatelyMarkedUndeveloped) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The braking function meets its requirements", true));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    // Marking a goal undeveloped is an honest statement of work remaining, not
    // a defect. Flagging it would train the agent to hide open work.
    EXPECT_FALSE(Mentions(findings, "EV.1", "G1"));
}

// AR.1 -- let structure carry the argument. A strategy that develops into
// nothing announces a decomposition the argument never performs, so the element
// is not doing the job its role says it does. This cites AR.1 rather than AR.2
// because AR.2's catalog check (`check-explicit-strategy`) asks the opposite
// question, about the decomposition above; see the AR.2 tests below.
TEST(SccgStagedChecks, AR1_FlagsAStrategyThatDevelopsIntoNothing) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Top goal", true));
    model.elements.push_back(Strategy("S1", "Argue over hazards"));
    model.elements.push_back(Supports("R1", "S1", "G1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"S1"});

    ASSERT_TRUE(Mentions(findings, "AR.1", "S1"));
    for (const core::sccg::StagedFinding& finding : findings) {
        if (finding.guideline_id == "AR.1" && finding.element_id == "S1") {
            EXPECT_EQ(finding.severity, core::sccg::FindingSeverity::Problem);
        }
    }
}

TEST(SccgStagedChecks, AR1_AcceptsAStrategyWithASubGoal) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Top goal", true));
    model.elements.push_back(Strategy("S1", "Argue over hazards"));
    model.elements.push_back(Claim("G2", "Hazard H1 is mitigated", true));
    model.elements.push_back(Supports("R1", "S1", "G1"));
    model.elements.push_back(Supports("R2", "G2", "S1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"S1"});

    EXPECT_FALSE(Mentions(findings, "AR.1", "S1"));
}

// AR.2 -- state the inference step. The catalog's `check-explicit-strategy` is
// a check on the decomposition: "whether each decomposition has an explicit
// reasoning step stating how children support the parent". It fires on the
// parent, which is what AR.2's own bad example is.
TEST(SccgStagedChecks, AR2_FlagsADecompositionWithNoReasoningStep) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Autonomy function safety is acceptable", true));
    model.elements.push_back(Claim("G2", "Perception safety is acceptable", true));
    model.elements.push_back(Claim("G3", "Planning safety is acceptable", true));
    model.elements.push_back(Supports("R1", "G2", "G1"));
    model.elements.push_back(Supports("R2", "G3", "G1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    ASSERT_TRUE(Mentions(findings, "AR.2", "G1"));
    for (const core::sccg::StagedFinding& finding : findings) {
        if (finding.guideline_id == "AR.2") {
            EXPECT_EQ(finding.check_id, "check-explicit-strategy");
            // SCCG publishes this as a boolean_candidate: a candidate finding a
            // reviewer still judges, not a defect.
            EXPECT_EQ(finding.severity, core::sccg::FindingSeverity::Advisory);
        }
    }
}

TEST(SccgStagedChecks, AR2_AcceptsADecompositionThroughAStrategy) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Autonomy function safety is acceptable", true));
    model.elements.push_back(Strategy("S1", "Break the claim down by UL 4600 autonomy topic"));
    model.elements.push_back(Claim("G2", "Perception safety is acceptable", true));
    model.elements.push_back(Claim("G3", "Planning safety is acceptable", true));
    model.elements.push_back(Supports("R1", "S1", "G1"));
    model.elements.push_back(Supports("R2", "G2", "S1"));
    model.elements.push_back(Supports("R3", "G3", "S1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    EXPECT_FALSE(Mentions(findings, "AR.2", "G1"));
}

// Narrowed deliberately: one child is a refinement, not a decomposition, and
// AR.2 asks why *these* children were chosen.
TEST(SccgStagedChecks, AR2_AcceptsASingleSubClaim) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Top goal", true));
    model.elements.push_back(Claim("G2", "The one thing it rests on", true));
    model.elements.push_back(Supports("R1", "G2", "G1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    EXPECT_FALSE(Mentions(findings, "AR.2", "G1"));
}

// A claim resting directly on evidence is not decomposed at all.
TEST(SccgStagedChecks, AR2_AcceptsAClaimSupportedOnlyByEvidence) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Top goal", true));
    model.elements.push_back(Solution("E1", "Test report TR-1 rev C"));
    model.elements.push_back(Solution("E2", "Test report TR-2 rev A"));
    // AssertedEvidence, not AssertedInference: it is what gives an artifact the
    // Solution role, and with an inference these would be generic nodes and the
    // test would pass without ever exercising the evidence case.
    model.elements.push_back(Evidences("R1", "E1", "G1"));
    model.elements.push_back(Evidences("R2", "E2", "G1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    EXPECT_FALSE(Mentions(findings, "AR.2", "G1"));
}

// The case that matters for an agent: it attaches a second sub-claim to a goal
// that already existed. Only the new child and the new relationship are
// reported changed, so without resolving relationships to their endpoints the
// parent-side check would never see the decomposition it is about.
TEST(SccgStagedChecks, AR2_FiresWhenAChildIsAttachedToAnExistingGoal) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Autonomy function safety is acceptable", true));
    model.elements.push_back(Claim("G2", "Perception safety is acceptable", true));
    model.elements.push_back(Claim("G3", "Planning safety is acceptable", true));
    model.elements.push_back(Supports("R1", "G2", "G1"));
    model.elements.push_back(Supports("R2", "G3", "G1"));

    // G1 is untouched by this change set; the agent added G3 and its edge.
    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G3", "R2"});

    EXPECT_TRUE(Mentions(findings, "AR.2", "G1"));
}

// CL.5 -- bound vague and universal qualifiers. This is the one lexical check,
// so it is advisory and quotes the term it objected to.
TEST(SccgStagedChecks, CL5_FlagsAnUnboundedQualifier) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The vehicle is safe", true));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    ASSERT_TRUE(Mentions(findings, "CL.5", "G1"));
    for (const core::sccg::StagedFinding& finding : findings) {
        if (finding.guideline_id == "CL.5") {
            EXPECT_EQ(finding.severity, core::sccg::FindingSeverity::Advisory);
            EXPECT_NE(finding.detail.find("safe"), std::string::npos);
            // The term rides as a parameter too, so a display surface can
            // interpolate it into a translated template instead of parsing
            // the English sentence back apart.
            EXPECT_EQ(finding.check_id, "check-bounded-qualifiers");
            ASSERT_EQ(finding.params.size(), 1u);
            EXPECT_EQ(finding.params[0], "safe");
        }
    }
}

// A lexical check that fires on substrings gets ignored, which is worse than not
// having it: "install" is not "all", and "safety" is not "safe".
TEST(SccgStagedChecks, CL5_DoesNotFireOnSubstringsOfLongerWords) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The safety case installation procedure was followed", true));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    EXPECT_FALSE(Mentions(findings, "CL.5", "G1"));
}

// AR.1 -- let structure carry the argument. Evidence is where the argument
// rests; things hanging beneath it are not playing the role the structure says.
TEST(SccgStagedChecks, AR1_FlagsASolutionWithChildren) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Top goal", true));
    model.elements.push_back(Solution("Sn1"));
    model.elements.push_back(Claim("G2", "A goal under evidence", true));
    model.elements.push_back(Evidences("R1", "Sn1", "G1"));
    model.elements.push_back(Supports("R2", "G2", "Sn1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"Sn1"});

    EXPECT_TRUE(Mentions(findings, "AR.1", "Sn1"));
}

// An agent should not be handed findings about parts of the argument it did not
// write. They are the user's business, and burying the agent's own mistakes in
// somebody else's is how a check stops being read.
TEST(SccgStagedChecks, ReportsOnlyWhatTheChangeSetTouched) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "An untouched claim with no support"));
    model.elements.push_back(Claim("G2", "A claim this change set added"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G2"});

    EXPECT_TRUE(Mentions(findings, "EV.1", "G2"));
    EXPECT_FALSE(Mentions(findings, "EV.1", "G1"));
}

TEST(SccgStagedChecks, ReportsNothingWhenNothingWasStaged) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "A claim with no support"));

    EXPECT_TRUE(core::sccg::CheckStagedArgument(model, {}).empty());
}

// CL.2 -- one main claim per goal. "Safe and secure" is two claims needing two
// evidence sets, and bundling them hides whichever one would have failed.
TEST(SccgStagedChecks, CL2_FlagsTwoPropertiesJoinedInOneGoal) {
    const std::vector<core::sccg::StagedFinding> findings = CheckClaimText("Planning software is safe and secure");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-single-property", "G1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "CL.2");
    EXPECT_EQ(finding->severity, core::sccg::FindingSeverity::Advisory);
    ASSERT_EQ(finding->params.size(), 2u);
    EXPECT_EQ(finding->params[0], "safe");
    EXPECT_EQ(finding->params[1], "secure");
}

// A conjunction between things that are not evaluative properties is ordinary
// English, not a bundled claim.
TEST(SccgStagedChecks, CL2_AcceptsACompoundNounConjunction) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("The hazard log and the FMEA are consistent");

    EXPECT_EQ(FindByCheck(findings, "check-single-property", "G1"), nullptr);
}

// CL.6 -- different logical steps in one claim. "Mitigated and validated" are
// two review questions with two evidence sets.
TEST(SccgStagedChecks, CL6_FlagsChainedLifecycleSteps) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("All identified hazards have been mitigated and validated");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-claim-step-mixing", "G1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "CL.6");
    ASSERT_EQ(finding->params.size(), 2u);
    EXPECT_EQ(finding->params[0], "mitigated");
    EXPECT_EQ(finding->params[1], "validated");
}

TEST(SccgStagedChecks, CL6_AcceptsOneStepPerClaim) {
    const std::vector<core::sccg::StagedFinding> findings = CheckClaimText("Defined mitigations are implemented");

    EXPECT_EQ(FindByCheck(findings, "check-claim-step-mixing", "G1"), nullptr);
}

// RD.1 -- signpost each element's role. A claim that carries "because" is a
// claim and its argument compressed into one sentence, and a reviewer can no
// longer challenge them separately.
TEST(SccgStagedChecks, RD1_FlagsReasoningInsideAClaim) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("The braking controller is acceptable because tests passed");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-element-signposting", "G1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "RD.1");
    ASSERT_EQ(finding->params.size(), 1u);
    EXPECT_EQ(finding->params[0], "because");
}

TEST(SccgStagedChecks, RD1_AcceptsAClaimStatedAlone) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("Braking controller performance meets the defined criteria");

    EXPECT_EQ(FindByCheck(findings, "check-element-signposting", "G1"), nullptr);
}

// RD.4 -- no promotional language. The words come from the guideline's own
// detection hints.
TEST(SccgStagedChecks, RD4_FlagsPromotionalLanguage) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("A world-class safety architecture delivers assurance");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-promotional-language", "G1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "RD.4");
    ASSERT_EQ(finding->params.size(), 1u);
    EXPECT_EQ(finding->params[0], "world-class");
}

TEST(SccgStagedChecks, RD4_AcceptsAPlainStatement) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("The architecture prevents hazardous actuation for fault classes F1-F4");

    EXPECT_EQ(FindByCheck(findings, "check-promotional-language", "G1"), nullptr);
}

// EV.7 -- evidence under document control. A reference with no owner, version,
// date or status could be anything, assessed at any time.
TEST(SccgStagedChecks, EV7_FlagsAnUncontrolledEvidenceReference) {
    const std::vector<core::sccg::StagedFinding> findings = CheckSolutionText("Test summary on team wiki");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-evidence-control-attributes", "Sn1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "EV.7");
}

TEST(SccgStagedChecks, EV7_AcceptsAControlledReference) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckSolutionText("Test summary TSR-11, rev D, approved 2026-03-14");

    EXPECT_EQ(FindByCheck(findings, "check-evidence-control-attributes", "Sn1"), nullptr);
}

// EV.8 -- cite fixed evidence. A wiki page is whatever it says today, which is
// not what the reviewer approved.
TEST(SccgStagedChecks, EV8_FlagsAMutableSourceWithNothingFixingItsState) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckSolutionText("See braking verification page in Confluence");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-evidence-state-fixed", "Sn1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "EV.8");
    ASSERT_EQ(finding->params.size(), 1u);
    EXPECT_EQ(finding->params[0], "confluence");
}

TEST(SccgStagedChecks, EV8_AcceptsAnArchivedSnapshotOfAMutableSource) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckSolutionText("Archived snapshot of Confluence page CP-118, version 17, captured 2026-03-11");

    EXPECT_EQ(FindByCheck(findings, "check-evidence-state-fixed", "Sn1"), nullptr);
}

// EV.4 -- cite the part of the artifact that supports the claim. The last of
// SCCG's five published pre-checks to be implemented; the registry had carried
// it since the catalog was first parsed.
TEST(SccgStagedChecks, EV4_FlagsAWholeArtifactCitation) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Braking is verified", true));
    model.elements.push_back(Solution("Sn1", "See validation report, approved rev C"));
    model.elements.push_back(Evidences("R1", "Sn1", "G1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"Sn1"});

    ASSERT_TRUE(Mentions(findings, "EV.4", "Sn1"));
    for (const core::sccg::StagedFinding& finding : findings) {
        if (finding.guideline_id == "EV.4") {
            EXPECT_EQ(finding.check_id, "check-evidence-citation-precision");
            EXPECT_EQ(finding.severity, core::sccg::FindingSeverity::Advisory);
        }
    }
}

TEST(SccgStagedChecks, EV4_AcceptsACitationNamingThePartItRestsOn) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Braking is verified", true));
    model.elements.push_back(
        Solution("Sn1", "Validation report VR-04 rev C, section 6.3, table 12, scenarios S14-S21"));
    model.elements.push_back(Evidences("R1", "Sn1", "G1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"Sn1"});

    EXPECT_FALSE(Mentions(findings, "EV.4", "Sn1"));
}

// A dotted number points inside an artifact as well as the word does. Without
// this the check would complain about a citation that is already precise, which
// is what teaches a reviewer to ignore findings.
TEST(SccgStagedChecks, EV4_AcceptsADottedSectionNumberWithoutTheWord) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Braking is verified", true));
    model.elements.push_back(Solution("Sn1", "Validation report VR-04 rev C, 6.3.2"));
    model.elements.push_back(Evidences("R1", "Sn1", "G1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"Sn1"});

    EXPECT_FALSE(Mentions(findings, "EV.4", "Sn1"));
}

// LF.3 -- do not argue from ignorance. "No failures were found" reports the
// reach of the search, not the safety of the system.
TEST(SccgStagedChecks, LF3_FlagsArguingFromAbsence) {
    const std::vector<core::sccg::StagedFinding> findings = CheckClaimText("No further hazards were found in review");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-completeness-vs-absence", "G1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "LF.3");
}

TEST(SccgStagedChecks, LF3_AcceptsArguingFromWhatWasApplied) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("Complementary hazard identification methods have been applied");

    EXPECT_EQ(FindByCheck(findings, "check-completeness-vs-absence", "G1"), nullptr);
}

// A support cycle is structurally wrong wherever it appears: an argument that
// assumes its own conclusion establishes nothing. Reported when the staged
// operations touch any element of the cycle.
TEST(SccgStagedChecks, FlagsASupportCycleTouchingTheChange) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "First of a cycle", true));
    model.elements.push_back(Claim("G2", "Second of a cycle", true));
    model.elements.push_back(Supports("R1", "G2", "G1"));
    model.elements.push_back(Supports("R2", "G1", "G2"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    bool cycle_reported = false;
    for (const core::sccg::StagedFinding& finding : findings) {
        if (finding.check_id != "check-circular-support") {
            continue;
        }
        cycle_reported = true;
        EXPECT_EQ(finding.severity, core::sccg::FindingSeverity::Problem);
    }
    EXPECT_TRUE(cycle_reported);
}

// Every finding names its guideline, the catalog's check id, and SCCG's own
// wording, so a reviewer can check the rule rather than take the tool's word
// for it, and a display surface can translate by a stable key.
TEST(SccgStagedChecks, EveryFindingCitesTheGuidelineItServes) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The vehicle is safe"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"G1"});

    ASSERT_FALSE(findings.empty());
    for (const core::sccg::StagedFinding& finding : findings) {
        EXPECT_FALSE(finding.guideline_id.empty());
        EXPECT_FALSE(finding.check_id.empty());
        EXPECT_FALSE(finding.statement.empty());
        EXPECT_FALSE(finding.detail.empty());
    }
}

// The catalog's own examples are the acceptance corpus for every lexical
// check: it must fire on the guideline's `bad` example and stay silent on its
// `good` one, read from the catalog at test time rather than copied here. A
// check that cannot tell SCCG's own bad from its own good has no business
// commenting on anyone else's argument.
TEST(SccgStagedChecks, EveryLexicalCheckSeparatesTheCatalogsOwnExamples) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;

    struct CorpusEntry {
        const char* guideline_id;
        const char* check_id;
        bool solution_role; // the evidence checks judge Solution text
    };
    // CL.3 is deliberately absent: its `good` example is a whole structure --
    // a claim, its context, its reasoning and six sub-claims -- and feeding
    // that blob in as one claim's text makes it long by construction. The
    // check is right and the corpus shape is wrong for it, so CL.3 gets its own
    // tests below instead of a false failure here.
    const std::vector<CorpusEntry> corpus{
        {"CL.5", "check-bounded-qualifiers", false},
        {"CL.2", "check-single-property", false},
        {"CL.4", "check-claim-ambiguity", false},
        {"CL.6", "check-claim-step-mixing", false},
        {"RD.1", "check-element-signposting", false},
        {"RD.4", "check-promotional-language", false},
        {"LF.3", "check-completeness-vs-absence", false},
        {"EV.7", "check-evidence-control-attributes", true},
        {"EV.8", "check-evidence-state-fixed", true},
    };

    for (const CorpusEntry& entry : corpus) {
        const parser::Guideline* guideline = catalog.document.FindGuidelineById(entry.guideline_id);
        ASSERT_NE(guideline, nullptr) << entry.guideline_id;
        ASSERT_FALSE(guideline->examples.bad.empty()) << entry.guideline_id;
        ASSERT_FALSE(guideline->examples.good.empty()) << entry.guideline_id;

        const std::vector<core::sccg::StagedFinding> on_bad =
            entry.solution_role ? CheckSolutionText(guideline->examples.bad) : CheckClaimText(guideline->examples.bad);
        EXPECT_NE(FindByCheck(on_bad, entry.check_id, entry.solution_role ? "Sn1" : "G1"), nullptr)
            << entry.check_id << " did not fire on the catalog's own bad example:\n"
            << guideline->examples.bad;

        const std::vector<core::sccg::StagedFinding> on_good = entry.solution_role
                                                                   ? CheckSolutionText(guideline->examples.good)
                                                                   : CheckClaimText(guideline->examples.good);
        EXPECT_EQ(FindByCheck(on_good, entry.check_id, entry.solution_role ? "Sn1" : "G1"), nullptr)
            << entry.check_id << " fired on the catalog's own good example:\n"
            << guideline->examples.good;
    }
}

// CL.4 -- ambiguity. Deferred until SCCG published a list to derive from;
// 0.7.0's `ambiguous_qualifier` and `hedging_adverb` markers are that list.
TEST(SccgStagedChecks, CL4_FlagsAHedgedClaimAndQuotesTheTerm) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("Software requirements development is acceptably complete");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-claim-ambiguity", "G1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "CL.4");
    EXPECT_EQ(finding->severity, core::sccg::FindingSeverity::Advisory);
    ASSERT_EQ(finding->params.size(), 1u);
    EXPECT_EQ(finding->params[0], "acceptably");
}

// "Adequate" and "appropriate" are on CL.4's and CL.5's lists both. One word,
// one advisory: two findings quoting the same term read as two problems.
TEST(SccgStagedChecks, CL4_DoesNotRepeatATermCL5AlreadyObjectedTo) {
    const std::vector<core::sccg::StagedFinding> findings = CheckClaimText("The mitigation is adequate");

    EXPECT_NE(FindByCheck(findings, "check-bounded-qualifiers", "G1"), nullptr);
    EXPECT_EQ(FindByCheck(findings, "check-claim-ambiguity", "G1"), nullptr);
}

TEST(SccgStagedChecks, CL4_StaysSilentOnAClaimThatSaysWhatItAsserts) {
    const std::vector<core::sccg::StagedFinding> findings = CheckClaimText(
        "The process used to develop software functional requirements satisfies the defined assurance criteria");

    EXPECT_EQ(FindByCheck(findings, "check-claim-ambiguity", "G1"), nullptr);
}

// CL.3 -- the essay in the box, at the word count SCCG publishes rather than one
// this tool picked.
TEST(SccgStagedChecks, CL3_FlagsAClaimPastThePublishedWordCount) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;
    const parser::Guideline* cl3 = catalog.document.FindGuidelineById("CL.3");
    ASSERT_NE(cl3, nullptr);
    ASSERT_FALSE(cl3->tool.thresholds.empty());
    const int limit = static_cast<int>(cl3->tool.thresholds[0].value);
    ASSERT_GT(limit, 0);

    std::string long_claim = "The system is acceptable";
    for (int word = 0; word <= limit; ++word) {
        long_claim += " and the subsystem is acceptable";
    }
    const std::vector<core::sccg::StagedFinding> findings = CheckClaimText(long_claim);

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-claim-length-and-role-mixing", "G1");
    ASSERT_NE(finding, nullptr);
    EXPECT_EQ(finding->guideline_id, "CL.3");
    ASSERT_EQ(finding->params.size(), 2u);
    EXPECT_EQ(finding->params[1], std::to_string(limit));

    // And a short claim is not flagged, so the threshold is doing the deciding.
    EXPECT_EQ(
        FindByCheck(CheckClaimText("The braking system stops the vehicle"), "check-claim-length-and-role-mixing", "G1"),
        nullptr);
}

// The other half of CL.3's published condition. Reporting only the word count
// under this check id would let an agent read a silent result as the whole
// check having run.
TEST(SccgStagedChecks, CL3_FlagsATopicListWrittenAsProse) {
    const std::vector<core::sccg::StagedFinding> findings =
        CheckClaimText("The design is sound, including faults, failures and interfaces");

    const core::sccg::StagedFinding* finding = FindByCheck(findings, "check-claim-length-and-role-mixing", "G1");
    ASSERT_NE(finding, nullptr);
    ASSERT_EQ(finding->params.size(), 1u);
    EXPECT_EQ(finding->params[0], "including");
}

// CL.5's suppression, published as `bounding_marker` and new with 0.7.0: a claim
// that states the bound the guideline asks for is not a claim missing it.
TEST(SccgStagedChecks, CL5_StaysSilentWhenTheClaimCarriesABoundingMarker) {
    EXPECT_NE(FindByCheck(CheckClaimText("The vehicle is safe"), "check-bounded-qualifiers", "G1"), nullptr);
    EXPECT_EQ(FindByCheck(CheckClaimText("The vehicle is safe as defined in the acceptance criterion"),
                          "check-bounded-qualifiers",
                          "G1"),
              nullptr);
}

// Every word these checks match on is one SCCG publishes. A term invented here
// would be this tool's rule wearing SCCG's citation -- which is what the
// hand-maintained lists were, and why they were narrower than the guideline in
// ways nothing recorded.
TEST(SccgStagedChecks, EveryLexicalCheckMatchesOnlyPublishedMarkerTerms) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;

    struct MarkerCase {
        const char* guideline_id;
        const char* check_id;
        const char* kind;
        const char* term;
        bool solution_role;
    };
    const std::vector<MarkerCase> marker_cases{
        {"CL.5", "check-bounded-qualifiers", "unbounded_evaluative_term", "nominal", false},
        {"CL.4", "check-claim-ambiguity", "ambiguous_qualifier", "satisfactory", false},
        {"RD.1", "check-element-signposting", "inference_connective", "hence", false},
        {"RD.4", "check-promotional-language", "promotional_term", "state-of-the-art", false},
        {"LF.3", "check-completeness-vs-absence", "absence_phrase", "no known", false},
        {"EV.8", "check-evidence-state-fixed", "mutable_source_marker", "trunk", true},
    };

    for (const MarkerCase& marker_case : marker_cases) {
        SCOPED_TRACE(marker_case.check_id);
        // The term is published -- if SCCG drops it, this fails here rather than
        // silently leaving the check matching a word nobody stands behind.
        const parser::Guideline* guideline = catalog.document.FindGuidelineById(marker_case.guideline_id);
        ASSERT_NE(guideline, nullptr);
        bool published = false;
        for (const parser::GuidelineMarker& marker : guideline->tool.markers) {
            if (marker.kind != marker_case.kind) {
                continue;
            }
            published = published ||
                        std::find(marker.terms.begin(), marker.terms.end(), marker_case.term) != marker.terms.end();
        }
        ASSERT_TRUE(published) << marker_case.term << " is not published under " << marker_case.kind;

        // And the check actually matches it, which the hand-written lists did
        // not: none of these six terms was in them.
        const std::string sentence = std::string("The result is ") + marker_case.term + " for the assessed scope";
        const std::vector<core::sccg::StagedFinding> findings =
            marker_case.solution_role ? CheckSolutionText(sentence) : CheckClaimText(sentence);
        EXPECT_NE(FindByCheck(findings, marker_case.check_id, marker_case.solution_role ? "Sn1" : "G1"), nullptr)
            << sentence;
    }
}

// The statements the checks embed are quotes, not paraphrases. Each one must
// still open the guideline it cites -- embedded as a prefix, because a check
// may deliberately quote only the statement's first sentence. A constant that
// drifts from the catalog would put SCCG's name on words SCCG never wrote.
TEST(SccgStagedChecks, EveryEmbeddedStatementStillQuotesTheCatalog) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;

    // One model tripping every check, so every embedded statement is
    // compared. Built element by element against `ImplementedCheckIds` rather
    // than grown by hand: the model this replaced reached seven of the thirteen
    // checks, so six embedded statements were never compared and one of them
    // was a paraphrase the test was supposed to catch.
    parser::AssuranceCase model;
    std::vector<std::string> changed;
    const auto claim = [&](const char* id, const std::string& claim_text) {
        model.elements.push_back(Claim(id, claim_text, /*undeveloped=*/true));
        changed.emplace_back(id);
    };

    // EV.1 -- unsupported and not marked undeveloped.
    model.elements.push_back(Claim("G_UNSUPPORTED", "The braking function meets its requirements"));
    changed.emplace_back("G_UNSUPPORTED");

    // AR.2 -- two sub-claims, no reasoning step between them and the parent.
    model.elements.push_back(Claim("G_DECOMPOSED", "The design is acceptable", /*undeveloped=*/true));
    claim("G_SUB_A", "Fault handling is acceptable");
    claim("G_SUB_B", "Failure response is acceptable");
    model.elements.push_back(Supports("R_SUB_A", "G_SUB_A", "G_DECOMPOSED"));
    model.elements.push_back(Supports("R_SUB_B", "G_SUB_B", "G_DECOMPOSED"));
    changed.emplace_back("G_DECOMPOSED");

    // AR.1 -- a strategy developing into nothing, and a solution with a child.
    model.elements.push_back(Strategy("S_EMPTY", "Argue over hazards"));
    model.elements.push_back(Claim("G_STRATEGY_PARENT", "Top goal", /*undeveloped=*/true));
    model.elements.push_back(Supports("R_STRATEGY", "S_EMPTY", "G_STRATEGY_PARENT"));
    changed.emplace_back("S_EMPTY");
    model.elements.push_back(Claim("G_EVIDENCED", "A goal with evidence", /*undeveloped=*/true));
    // Not "wiki page": "page" is one of EV.4's citation-precision markers, so a
    // reference naming it reads as precise and EV.4 stays silent -- which is
    // how this model first passed while never reaching EV.4's quote.
    model.elements.push_back(Solution("SN_WITH_CHILD", "Wiki entry holding the results"));
    model.elements.push_back(Evidences("R_EVIDENCE", "SN_WITH_CHILD", "G_EVIDENCED"));
    model.elements.push_back(Claim("G_UNDER_EVIDENCE", "A goal under evidence", /*undeveloped=*/true));
    model.elements.push_back(Supports("R_UNDER", "G_UNDER_EVIDENCE", "SN_WITH_CHILD"));
    changed.emplace_back("SN_WITH_CHILD");
    // EV.4, EV.7 and EV.8 all judge that solution's text: it names no part of an
    // artifact, carries no control attribute, and cites a mutable source with
    // nothing fixing its state.

    // AR.1 again, as a support cycle.
    model.elements.push_back(Claim("G_CYCLE_A", "First of a cycle", /*undeveloped=*/true));
    model.elements.push_back(Claim("G_CYCLE_B", "Second of a cycle", /*undeveloped=*/true));
    model.elements.push_back(Supports("R_CYCLE_A", "G_CYCLE_B", "G_CYCLE_A"));
    model.elements.push_back(Supports("R_CYCLE_B", "G_CYCLE_A", "G_CYCLE_B"));
    changed.emplace_back("G_CYCLE_A");
    changed.emplace_back("G_CYCLE_B");

    // One claim per lexical check, so a check that stops firing is named rather
    // than hidden behind another finding on the same element.
    claim("G_CL5", "The vehicle is safe");
    claim("G_CL4", "Requirements development is acceptably complete");
    claim("G_CL2", "Planning software is safe and secure");
    claim("G_CL6", "Hazards have been identified and validated");
    claim("G_CL3_LIST", "The design is sound, including faults and failures");
    claim("G_CL3_LENGTH",
          "The complete product concept and the system design and the operating envelope and the "
          "degraded modes and the transitions between them and the interfaces and the dependencies "
          "and the residual risks and the monitoring arrangements and the review record are in order");
    claim("G_RD1", "The vehicle stops in time because the brakes were tested");
    claim("G_RD4", "The team applied world-class engineering practice");
    claim("G_LF3", "There are no known hazards in the released configuration");

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, changed);

    ASSERT_FALSE(findings.empty());
    // Every check the tool says it implements has to appear here, or the loop
    // below compares fewer quotes than there are quotes.
    for (const std::string& check_id : core::sccg::ImplementedCheckIds()) {
        bool present = false;
        for (const core::sccg::StagedFinding& finding : findings) {
            present = present || finding.check_id == check_id;
        }
        EXPECT_TRUE(present) << check_id << " fired on nothing, so its embedded statement was never compared.";
    }
    for (const core::sccg::StagedFinding& finding : findings) {
        const parser::Guideline* guideline = catalog.document.FindGuidelineById(finding.guideline_id);
        ASSERT_NE(guideline, nullptr) << finding.guideline_id;
        EXPECT_EQ(guideline->statement.rfind(finding.statement, 0), 0u)
            << finding.guideline_id << ": the embedded statement is no longer how the catalog opens.\n"
            << "embedded: " << finding.statement << "\ncatalog:  " << guideline->statement;
    }
}
