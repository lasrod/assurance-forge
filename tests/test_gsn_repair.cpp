// Repairs for the defects the GSN v3 well-formedness checker reports.
//
// The point of these tests is not that a mutator mutates. It is that applying
// the repair a finding offers makes that finding go away, and takes nothing
// else with it — a validator whose fix does not fix, or fixes by deleting more
// than it said, is worse than no validator at all.

#include "core/relationship_editing.h"

#include "app/structure_problem_sync.h"
#include "core/element_factory.h"
#include "core/problems/gsn_wellformedness.h"
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

parser::SacmElement ArtifactReference(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "artifactreference";
    element.name = element.id;
    return element;
}

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

bool HasElement(const parser::AssuranceCase& model, const std::string& id) {
    return std::any_of(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == id;
    });
}

const parser::SacmElement* Find(const parser::AssuranceCase& model, const std::string& id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

bool HasFindingOfRule(const parser::AssuranceCase& model, core::GsnRule rule) {
    const std::vector<core::GsnFinding> findings = core::CheckGsnWellFormedness(model);
    return std::any_of(findings.begin(), findings.end(), [&](const core::GsnFinding& finding) {
        return finding.rule == rule;
    });
}

} // namespace

// ===== Removing a relationship =====

TEST(GsnRepairTest, RemovingARelationshipKeepsBothEndpoints) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), Inference("R1", {"G2"}, "G1")};

    std::string error;
    ASSERT_TRUE(core::RemoveRelationship(model, nullptr, "R1", error)) << error;

    EXPECT_FALSE(HasElement(model, "R1"));
    // The relationship was the claim that G2 supports G1. Withdrawing that claim
    // is not a decision to delete either goal — G2 becomes an orphan the user
    // can see and re-attach, rather than disappearing.
    EXPECT_TRUE(HasElement(model, "G1"));
    EXPECT_TRUE(HasElement(model, "G2"));
}

TEST(GsnRepairTest, RemovingARelationshipRejectsANodeId) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), Inference("R1", {"G2"}, "G1")};

    std::string error;
    EXPECT_FALSE(core::RemoveRelationship(model, nullptr, "G1", error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(HasElement(model, "G1")) << "a node must not be removed by the relationship path";
}

TEST(GsnRepairTest, GSN3_CORE_015_RemovingTheRelationshipClearsTheFinding) {
    parser::AssuranceCase model;
    // A sub-goal argued beneath a Solution: GSN says a Solution is a leaf.
    model.elements = {Goal("G1"), Goal("G2"), ArtifactReference("Sn1"), Evidence("R1", "Sn1", "G1"),
                      Inference("R2", {"G2"}, "Sn1")};
    ASSERT_TRUE(HasFindingOfRule(model, core::GsnRule::SupportedElementIsALeaf));

    std::string error;
    ASSERT_TRUE(core::RemoveRelationship(model, nullptr, "R2", error)) << error;

    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
    EXPECT_TRUE(HasElement(model, "Sn1"));
    EXPECT_TRUE(HasElement(model, "G2"));
}

// ===== Dropping a broken reference =====

TEST(GsnRepairTest, GSN3_CORE_007_DroppingABrokenReferenceKeepsTheRelationship) {
    parser::AssuranceCase model;
    // The inference still has a real sub-goal, so scrubbing the broken one
    // leaves a relationship that still relates something.
    model.elements = {Goal("G1"), Goal("G2"), Inference("R1", {"G2", "G-missing"}, "G1")};
    ASSERT_TRUE(HasFindingOfRule(model, core::GsnRule::UnresolvedEndpoint));

    bool removed_relationship = true;
    std::string error;
    ASSERT_TRUE(core::DropRelationshipReference(model, nullptr, "R1", "G-missing", removed_relationship, error))
        << error;

    EXPECT_FALSE(removed_relationship);
    EXPECT_TRUE(HasElement(model, "R1"));
    ASSERT_NE(Find(model, "R1"), nullptr);
    EXPECT_EQ(Find(model, "R1")->source_refs, std::vector<std::string>{"G2"});
    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
}

TEST(GsnRepairTest, GSN3_CORE_007_DroppingTheLastReferenceRemovesTheRelationship) {
    parser::AssuranceCase model;
    // The broken reference was the only source. Scrubbing it leaves an inference
    // that infers nothing from nothing, which SACM multiplicity does not allow.
    model.elements = {Goal("G1"), Inference("R1", {"G-missing"}, "G1")};

    bool removed_relationship = false;
    std::string error;
    ASSERT_TRUE(core::DropRelationshipReference(model, nullptr, "R1", "G-missing", removed_relationship, error))
        << error;

    EXPECT_TRUE(removed_relationship) << "the caller has to be able to tell the user the edge went too";
    EXPECT_FALSE(HasElement(model, "R1"));
    EXPECT_TRUE(HasElement(model, "G1"));
    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
}

TEST(GsnRepairTest, DroppingAReferenceTheRelationshipDoesNotHaveIsRefused) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), Inference("R1", {"G2"}, "G1")};

    bool removed_relationship = false;
    std::string error;
    EXPECT_FALSE(
        core::DropRelationshipReference(model, nullptr, "R1", "G-elsewhere", removed_relationship, error));
    EXPECT_FALSE(error.empty());
    ASSERT_NE(Find(model, "R1"), nullptr);
    EXPECT_EQ(Find(model, "R1")->source_refs, std::vector<std::string>{"G2"});
}

// ===== Moving a Strategy into the reasoning slot =====

TEST(GsnRepairTest, GSN3_CORE_002_MovingTheStrategyPreservesTheArgument) {
    parser::AssuranceCase model;
    // S1 is wired as an end of the inference. The author's intent — this strategy
    // explains this step — is expressible; it is in the wrong slot, so the repair
    // is a re-wiring and not a deletion.
    model.elements = {Goal("G1"), Goal("G2"), Strategy("S1"), Inference("R1", {"G2", "S1"}, "G1")};
    ASSERT_TRUE(HasFindingOfRule(model, core::GsnRule::StrategyUsedAsAssertion));

    std::string error;
    ASSERT_TRUE(core::MoveStrategyToReasoning(model, nullptr, "R1", "S1", error)) << error;

    ASSERT_NE(Find(model, "R1"), nullptr);
    EXPECT_EQ(Find(model, "R1")->reasoning_ref, "S1");
    EXPECT_EQ(Find(model, "R1")->source_refs, std::vector<std::string>{"G2"});
    EXPECT_TRUE(HasElement(model, "S1")) << "the strategy is re-wired, never deleted";
    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
}

// Choosing which of two strategies explains an inference is a judgement about
// the argument, and the tool does not get to make it.
TEST(GsnRepairTest, MovingAStrategyIsRefusedWhenTheInferenceAlreadyHasReasoning) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), Strategy("S1"), Strategy("S2"),
                      Inference("R1", {"G2", "S1"}, "G1", "S2")};

    std::string error;
    EXPECT_FALSE(core::MoveStrategyToReasoning(model, nullptr, "R1", "S1", error));
    EXPECT_NE(error.find("S2"), std::string::npos) << "the refusal should name the reasoning already there";
    ASSERT_NE(Find(model, "R1"), nullptr);
    EXPECT_EQ(Find(model, "R1")->reasoning_ref, "S2");
}

TEST(GsnRepairTest, MovingAStrategyIsRefusedOnANonInference) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), ArtifactReference("Sn1"), Strategy("S1"), Evidence("R1", "S1", "G1")};

    std::string error;
    EXPECT_FALSE(core::MoveStrategyToReasoning(model, nullptr, "R1", "S1", error));
    EXPECT_FALSE(error.empty());
}

// ===== The undeveloped decorator =====

TEST(GsnRepairTest, GSN3_CORE_009_ClearingTheDecoratorClearsTheFinding) {
    parser::AssuranceCase model;
    parser::SacmElement top = Goal("G1");
    top.undeveloped = true;
    model.elements = {top, Goal("G2"), Inference("R1", {"G2"}, "G1")};
    ASSERT_TRUE(HasFindingOfRule(model, core::GsnRule::UndevelopedElementHasSupport));

    bool old_value = false;
    std::string error;
    ASSERT_TRUE(core::SetElementUndeveloped(model, nullptr, "G1", false, old_value, error)) << error;

    EXPECT_TRUE(old_value);
    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
}

// ===== Renumbering a duplicated identifier =====

TEST(GsnRepairTest, GSN3_CORE_010_RenumberingPicksTheNextFreeIdentifier) {
    parser::AssuranceCase model;
    parser::SacmElement collides = Goal("G2");
    collides.gsn_identifier = "G1";
    model.elements = {Goal("G1"), collides, Goal("G3")};
    ASSERT_TRUE(HasFindingOfRule(model, core::GsnRule::DuplicateNotationIdentifier));

    // G1 and G3 are taken, so the next free identifier under the "G" prefix is G2.
    const std::string next = core::NextFreeGsnIdentifier(model, "G2");
    EXPECT_EQ(next, "G2");

    std::string old_identifier;
    std::string error;
    ASSERT_TRUE(core::SetGsnIdentifier(model, nullptr, "G2", next, old_identifier, error)) << error;
    EXPECT_EQ(old_identifier, "G1");
    EXPECT_TRUE(core::CheckGsnWellFormedness(model).empty());
}

// The prefix comes from the identifier the element already answers to, not from
// its kind: an ArtifactReference is a Solution or a Context depending only on
// how it is wired, so kind cannot choose between "Sn" and "C".
TEST(GsnRepairTest, GSN3_CORE_010_RenumberingKeepsTheAuthorsOwnPrefix) {
    parser::AssuranceCase model;
    parser::SacmElement context = ArtifactReference("X1");
    context.gsn_identifier = "C4";
    parser::SacmElement solution = ArtifactReference("X2");
    solution.gsn_identifier = "Sn7";
    model.elements = {context, solution};

    EXPECT_EQ(core::NextFreeGsnIdentifier(model, "X1"), "C1");
    EXPECT_EQ(core::NextFreeGsnIdentifier(model, "X2"), "Sn1");
}

TEST(GsnRepairTest, RenumberingAnAcpScopedIdentifierStaysInItsScope) {
    parser::AssuranceCase model;
    parser::SacmElement scoped = Goal("ACP1_G2");
    scoped.gsn_identifier = "ACP1_G1";
    model.elements = {Goal("ACP1_G1"), scoped};

    EXPECT_EQ(core::NextFreeGsnIdentifier(model, "ACP1_G2"), "ACP1_G2");
}

// ===== The quick-fix payload the Problems panel carries =====

TEST(GsnRepairSyncTest, EveryFindingOffersARepair) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), ArtifactReference("Sn1"), Evidence("R1", "Sn1", "G1"),
                      Inference("R2", {"G2"}, "Sn1"), Inference("R3", {"G-missing"}, "G1")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);

    ASSERT_FALSE(problems.GetProblems().empty());
    for (const core::ProblemItem& problem : problems.GetProblems()) {
        EXPECT_FALSE(problem.quick_fix_label.empty())
            << "problem " << problem.type << " reports a defect with no way to act on it";
        app::GsnRepairPayload payload;
        EXPECT_TRUE(app::DecodeGsnRepairPayload(problem.quick_fix_payload, payload))
            << "problem " << problem.type << " has an undecodable repair payload";
    }
}

TEST(GsnRepairSyncTest, TheRepairPayloadCarriesTheRelationshipAndTheBrokenReference) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Inference("R1", {"G-missing"}, "G1")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);

    ASSERT_EQ(problems.GetProblems().size(), 1u);
    const core::ProblemItem& problem = problems.GetProblems().front();
    EXPECT_EQ(problem.type, "UnresolvedEndpoint");
    EXPECT_EQ(problem.quick_fix_label, "Drop broken reference");

    app::GsnRepairPayload payload;
    ASSERT_TRUE(app::DecodeGsnRepairPayload(problem.quick_fix_payload, payload));
    EXPECT_EQ(payload.relationship_id, "R1");
    EXPECT_EQ(payload.reference, "G-missing");
}

TEST(GsnRepairSyncTest, TheRepairDiffersByRuleRatherThanAlwaysDeleting) {
    parser::AssuranceCase model;
    model.elements = {Goal("G1"), Goal("G2"), Strategy("S1"), Inference("R1", {"G2", "S1"}, "G1")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);

    ASSERT_EQ(problems.GetProblems().size(), 1u);
    // Deleting the inference here would throw away an argument the author meant
    // to make; the strategy is merely in the wrong slot of it.
    EXPECT_EQ(problems.GetProblems().front().quick_fix_label, "Move to reasoning");
}
