#include "app/areas/staged_finding_text.h"

#include "core/sccg/staged_checks.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

// A staged finding's detail is written in English by core, which cannot include
// ui/i18n; the panels translate it by check id through StagedFindingText. These
// tests hold the two phrasings together: in English the helper must reproduce
// the finding's own detail byte for byte, because a divergence would ship two
// different sentences for one finding -- one to MCP clients, one to reviewers.

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

parser::SacmElement Supports(const std::string& id, const std::string& child, const std::string& parent) {
    parser::SacmElement element;
    element.id = id;
    element.type = "assertedinference";
    element.source_refs = {child};
    element.target_refs = {parent};
    return element;
}

parser::SacmElement Evidences(const std::string& id, const std::string& artifact, const std::string& claim) {
    parser::SacmElement element;
    element.id = id;
    element.type = "assertedevidence";
    element.source_refs = {artifact};
    element.target_refs = {claim};
    return element;
}

// One model that trips every mechanical check, so the round-trip below covers
// every template the helper knows. A check added without a template shows up
// here as a finding whose English came from the fallback -- which still
// passes -- so the coverage assertion counts distinct check ids too.
parser::AssuranceCase ModelWithEveryFinding() {
    parser::AssuranceCase model;
    // EV.1: no support, not undeveloped. CL.5: "safe" unbounded.
    model.elements.push_back(Claim("G1", "The vehicle is safe"));
    // AR.2: a strategy developing into nothing.
    model.elements.push_back(Claim("G2", "Top goal", true));
    model.elements.push_back(Strategy("S1", "Argue over hazards"));
    model.elements.push_back(Supports("R1", "S1", "G2"));
    // AR.1 role misuse: a solution with children.
    model.elements.push_back(Claim("G3", "Another goal", true));
    model.elements.push_back(Solution("Sn1"));
    model.elements.push_back(Claim("G4", "A goal under evidence", true));
    model.elements.push_back(Evidences("R2", "Sn1", "G3"));
    model.elements.push_back(Supports("R3", "G4", "Sn1"));
    // Circular support: G5 and G6 support each other.
    model.elements.push_back(Claim("G5", "First of a cycle", true));
    model.elements.push_back(Claim("G6", "Second of a cycle", true));
    model.elements.push_back(Supports("R4", "G6", "G5"));
    model.elements.push_back(Supports("R5", "G5", "G6"));
    return model;
}

} // namespace

TEST(StagedFindingText, EnglishOutputMatchesTheFindingDetailForEveryCheck) {
    const parser::AssuranceCase model = ModelWithEveryFinding();
    const std::vector<core::sccg::StagedFinding> findings =
        core::sccg::CheckStagedArgument(model, {"G1", "G2", "S1", "G3", "Sn1", "G4", "G5", "G6"});

    // Every template the helper knows must actually be exercised: a model that
    // stopped tripping a check would let its template rot unverified.
    std::vector<std::string> seen;
    for (const core::sccg::StagedFinding& finding : findings) {
        if (std::find(seen.begin(), seen.end(), finding.check_id) == seen.end()) {
            seen.push_back(finding.check_id);
        }
    }
    EXPECT_EQ(seen.size(), 5u) << "expected every mechanical check to fire in this model";

    // No Japanese catalog is loaded in tests, so AF_TR/trf return the English
    // template -- which must be the sentence core already wrote.
    for (const core::sccg::StagedFinding& finding : findings) {
        EXPECT_EQ(app::areas::StagedFindingText(finding), finding.detail)
            << "check " << finding.check_id << " translates through a template whose English "
            << "diverged from the detail core builds";
    }
}

TEST(StagedFindingText, AnUnknownCheckFallsBackToTheEnglishDetail) {
    core::sccg::StagedFinding finding;
    finding.guideline_id = "XX.1";
    finding.check_id = "check-added-without-a-template";
    finding.detail = "A sentence only core knows.";

    EXPECT_EQ(app::areas::StagedFindingText(finding), "A sentence only core knows.");
}
