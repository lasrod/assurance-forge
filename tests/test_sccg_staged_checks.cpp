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

parser::SacmElement Solution(const std::string& id) {
    parser::SacmElement element;
    element.id = id;
    element.type = "artifactreference";
    element.name = id;
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

// AR.2 -- state the inference step. A strategy that develops into nothing
// announces a decomposition the argument never performs.
TEST(SccgStagedChecks, AR2_FlagsAStrategyThatDevelopsIntoNothing) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Top goal", true));
    model.elements.push_back(Strategy("S1", "Argue over hazards"));
    model.elements.push_back(Supports("R1", "S1", "G1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"S1"});

    ASSERT_TRUE(Mentions(findings, "AR.2", "S1"));
    for (const core::sccg::StagedFinding& finding : findings) {
        if (finding.guideline_id == "AR.2") {
            EXPECT_EQ(finding.severity, core::sccg::FindingSeverity::Problem);
        }
    }
}

TEST(SccgStagedChecks, AR2_AcceptsAStrategyWithASubGoal) {
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "Top goal", true));
    model.elements.push_back(Strategy("S1", "Argue over hazards"));
    model.elements.push_back(Claim("G2", "Hazard H1 is mitigated", true));
    model.elements.push_back(Supports("R1", "S1", "G1"));
    model.elements.push_back(Supports("R2", "G2", "S1"));

    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {"S1"});

    EXPECT_FALSE(Mentions(findings, "AR.2", "S1"));
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

// The statements the checks embed are quotes, not paraphrases. Each one must
// still open the guideline it cites -- embedded as a prefix, because a check
// may deliberately quote only the statement's first sentence. A constant that
// drifts from the catalog would put SCCG's name on words SCCG never wrote.
TEST(SccgStagedChecks, EveryEmbeddedStatementStillQuotesTheCatalog) {
    core::GuidelineCatalog catalog;
    std::string error;
    ASSERT_TRUE(core::LoadGuidelineCatalog(catalog, error)) << error;

    // One model tripping every check, so every embedded statement is compared.
    parser::AssuranceCase model;
    model.elements.push_back(Claim("G1", "The vehicle is safe"));
    model.elements.push_back(Claim("G2", "Top goal", true));
    model.elements.push_back(Strategy("S1", "Argue over hazards"));
    model.elements.push_back(Supports("R1", "S1", "G2"));
    model.elements.push_back(Claim("G3", "Another goal", true));
    model.elements.push_back(Solution("Sn1"));
    model.elements.push_back(Claim("G4", "A goal under evidence", true));
    model.elements.push_back(Evidences("R2", "Sn1", "G3"));
    model.elements.push_back(Supports("R3", "G4", "Sn1"));
    model.elements.push_back(Claim("G5", "First of a cycle", true));
    model.elements.push_back(Claim("G6", "Second of a cycle", true));
    model.elements.push_back(Supports("R4", "G6", "G5"));
    model.elements.push_back(Supports("R5", "G5", "G6"));

    const std::vector<core::sccg::StagedFinding> findings =
        core::sccg::CheckStagedArgument(model, {"G1", "G2", "S1", "G3", "Sn1", "G4", "G5", "G6"});

    ASSERT_FALSE(findings.empty());
    for (const core::sccg::StagedFinding& finding : findings) {
        const parser::Guideline* guideline = catalog.document.FindGuidelineById(finding.guideline_id);
        ASSERT_NE(guideline, nullptr) << finding.guideline_id;
        EXPECT_EQ(guideline->statement.rfind(finding.statement, 0), 0u)
            << finding.guideline_id << ": the embedded statement is no longer how the catalog opens.\n"
            << "embedded: " << finding.statement << "\ncatalog:  " << guideline->statement;
    }
}
