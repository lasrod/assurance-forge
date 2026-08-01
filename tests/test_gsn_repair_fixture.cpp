// End-to-end over a case shaped like the deliberately-broken fixture: apply the
// repair each finding offers, in the order the Problems panel lists them, and
// the case must come out clean.
//
// The per-rule tests each prove one repair in isolation. This proves the set
// converges — that fixing one defect does not create another, and that a user
// working down the panel actually reaches zero.

#include "app/structure_problem_sync.h"

#include "core/element_factory.h"
#include "core/problems/gsn_wellformedness.h"
#include "core/problems/problems_manager.h"
#include "core/relationship_editing.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

parser::SacmElement Claim(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "claim";
    element.name = element.id;
    return element;
}

parser::SacmElement Reasoning(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "argumentreasoning";
    element.name = element.id;
    return element;
}

parser::SacmElement Reference(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "artifactreference";
    element.name = element.id;
    return element;
}

parser::SacmElement Relationship(std::string id,
                                 std::string type,
                                 std::vector<std::string> sources,
                                 std::string target,
                                 std::string reasoning = {}) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    element.source_refs = std::move(sources);
    element.target_refs = {std::move(target)};
    element.reasoning_ref = std::move(reasoning);
    return element;
}

// A sound little argument (G1 argued by S1 over G2, discharged by Sn1, scoped by
// C1) carrying one deliberate violation per rule -- the same shape as the
// hand-built fixture project used for manual testing.
parser::AssuranceCase BrokenCase() {
    parser::AssuranceCase model;
    parser::SacmElement undeveloped_goal = Claim("G2");
    undeveloped_goal.undeveloped = true; // GSN3-CORE-009: developed by R1 regardless

    model.elements = {
        Claim("G1"),
        undeveloped_goal,
        Reasoning("S1"),
        Reference("Sn1"),
        Reference("Sn2"),
        Reference("C1"),
        Relationship("R1", "assertedinference", {"G2"}, "G1", "S1"),
        Relationship("R2", "assertedevidence", {"Sn1"}, "G2"),
        Relationship("R3", "assertedcontext", {"C1"}, "G1"),
        // GSN3-CORE-015: a sub-goal argued beneath a Solution.
        Relationship("RBAD1", "assertedinference", {"G1"}, "Sn1"),
        // GSN3-CORE-007: an endpoint naming an element that is not here.
        Relationship("RBAD2", "assertedinference", {"G_NOT_HERE"}, "G1"),
        // GSN3-CORE-003: a Goal standing in the evidence position.
        Relationship("RBAD3", "assertedevidence", {"G1"}, "G2"),
        // GSN3-CORE-002: a Strategy wired as an end rather than as the reasoning.
        Relationship("RBAD4", "assertedinference", {"S1"}, "G1"),
        // GSN3-CORE-015: context declared on a Solution.
        Relationship("RBAD5", "assertedcontext", {"C1"}, "Sn2"),
    };
    return model;
}

// Applies the repair the panel offers for one problem, exactly as
// AppRuntime::ApplyGsnWellFormednessQuickFix routes it.
bool ApplyQuickFix(parser::AssuranceCase& model, const core::ProblemItem& problem, std::string& error) {
    app::GsnRepairPayload payload;
    if (!app::DecodeGsnRepairPayload(problem.quick_fix_payload, payload)) {
        error = "undecodable payload for " + problem.type;
        return false;
    }

    if (problem.type == "UnresolvedEndpoint") {
        bool removed = false;
        return core::DropRelationshipReference(model, nullptr, payload.relationship_id, payload.reference,
                                               removed, error);
    }
    if (problem.type == "ChallengeTargetUnresolved" || problem.type == "SupportedElementIsALeaf" ||
        problem.type == "ContextualizedElementIsALeaf" || problem.type == "EvidenceSourceIsNotASolution")
        return core::RemoveRelationship(model, nullptr, payload.relationship_id, error);
    if (problem.type == "StrategyUsedAsAssertion")
        return core::MoveStrategyToReasoning(model, nullptr, payload.relationship_id, problem.element_id,
                                             error);
    if (problem.type == "DuplicateNotationIdentifier") {
        std::string old_identifier;
        return core::SetGsnIdentifier(model, nullptr, problem.element_id,
                                      core::NextFreeGsnIdentifier(model, problem.element_id), old_identifier,
                                      error);
    }
    if (problem.type == "UndevelopedElementHasSupport") {
        bool old_value = false;
        return core::SetElementUndeveloped(model, nullptr, problem.element_id, false, old_value, error);
    }
    error = "no repair routed for " + problem.type;
    return false;
}

} // namespace

TEST(GsnRepairFixtureTest, TheBrokenCaseReportsOneFindingPerRule) {
    const std::vector<core::GsnFinding> findings = core::CheckGsnWellFormedness(BrokenCase());
    EXPECT_EQ(findings.size(), 6u);

    // A goal supported several ways is still one stale decorator. Reporting it
    // per supporting relationship would put the same fix on two rows.
    int undeveloped_findings = 0;
    for (const core::GsnFinding& finding : findings) {
        if (finding.rule == core::GsnRule::UndevelopedElementHasSupport)
            ++undeveloped_findings;
    }
    EXPECT_EQ(undeveloped_findings, 1) << "G2 is developed by both R1 and RBAD3";
}

// Wiring a goal beneath its own evidence also closes a support loop, so the
// panel carries a CircularArgument alongside the GSN findings. It has no
// one-click repair -- which relationship in a loop to withdraw is a judgement
// about the argument -- and this pins that it is reported rather than silently
// absent.
TEST(GsnRepairFixtureTest, ACircularArgumentIsReportedButHasNoQuickRepair) {
    parser::AssuranceCase model = BrokenCase();
    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);

    // Two, in fact: arguing G1 beneath its own evidence closes one loop through
    // Sn1, and the stray evidence relationship closes another straight back to
    // G1. The count is incidental; what matters is that they are reported and
    // that neither pretends to offer a repair.
    int cycles = 0;
    for (const core::ProblemItem& problem : problems.GetProblems()) {
        if (problem.type != "CircularArgument")
            continue;
        ++cycles;
        app::GsnRepairPayload payload;
        EXPECT_FALSE(app::DecodeGsnRepairPayload(problem.quick_fix_payload, payload))
            << "a cycle carries a navigation payload, not a repair";
    }
    EXPECT_GT(cycles, 0);
}

// Working down the panel one quick fix at a time reaches zero. Re-syncing after
// each repair is what the app does, so this also catches a repair that clears
// its own finding while creating a different one.
TEST(GsnRepairFixtureTest, ApplyingEveryOfferedRepairLeavesACleanCase) {
    parser::AssuranceCase model = BrokenCase();

    for (int pass = 0; pass < 16; ++pass) {
        core::ProblemsManager problems;
        app::SyncStructureProblems(problems, &model);

        // Take the first problem that offers a GSN repair. A CircularArgument
        // offers navigation instead, so it is stepped over here; withdrawing
        // the relationship that closes the loop is what clears it, and that
        // happens as a side effect of the GSN repairs below.
        const core::ProblemItem* repairable = nullptr;
        for (const core::ProblemItem& problem : problems.GetProblems()) {
            app::GsnRepairPayload payload;
            if (app::DecodeGsnRepairPayload(problem.quick_fix_payload, payload)) {
                repairable = &problem;
                break;
            }
        }
        if (!repairable)
            break;

        std::string error;
        ASSERT_TRUE(ApplyQuickFix(model, *repairable, error)) << repairable->type << ": " << error;
    }

    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
    // The loop closed only because a goal was argued beneath its own evidence,
    // so withdrawing that relationship clears the cycle too.
    core::ProblemsManager remaining;
    app::SyncStructureProblems(remaining, &model);
    EXPECT_TRUE(remaining.GetProblems().empty());

    // The argument that was sound is still here: repairs withdraw wrong
    // relationships, they do not delete the elements those relationships named.
    for (const char* id : {"G1", "G2", "S1", "Sn1", "Sn2", "C1", "R1", "R2", "R3"}) {
        const bool present = std::any_of(model.elements.begin(), model.elements.end(),
                                         [&](const parser::SacmElement& element) {
                                             return element.id == id;
                                         });
        EXPECT_TRUE(present) << id << " was collateral damage of a repair";
    }
}
