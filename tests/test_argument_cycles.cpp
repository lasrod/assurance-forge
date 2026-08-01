#include "core/problems/argument_cycles.h"

#include "app/structure_problem_sync.h"
#include "core/problems/problems_manager.h"

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

parser::SacmElement Strategy(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "argumentreasoning";
    element.name = element.id;
    return element;
}

parser::SacmElement Solution(std::string id) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = "artifactreference";
    element.name = element.id;
    return element;
}

// SACM AssertedInference: sources are the premises, target the conclusion. In
// GSN terms the target is supported by the sources.
parser::SacmElement
Inference(std::string id, std::vector<std::string> sources, std::string target, std::string reasoning = {}) {
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

bool HasCycle(const std::vector<core::ArgumentCycle>& cycles, const std::vector<std::string>& expected) {
    return std::any_of(
        cycles.begin(), cycles.end(), [&](const core::ArgumentCycle& cycle) { return cycle.element_ids == expected; });
}

} // namespace

TEST(ArgumentCyclesTest, AWellFormedArgumentHasNoCycles) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"),
                      Claim("G2"),
                      Claim("G3"),
                      Solution("Sn1"),
                      Inference("R1", {"G2", "G3"}, "G1"),
                      Evidence("R2", "Sn1", "G2")};

    EXPECT_TRUE(core::FindSupportCycles(model).empty());
}

TEST(ArgumentCyclesTest, DetectsATwoGoalCycle) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"), Claim("G2"), Inference("R1", {"G2"}, "G1"), Inference("R2", {"G1"}, "G2")};

    const std::vector<core::ArgumentCycle> cycles = core::FindSupportCycles(model);

    ASSERT_EQ(cycles.size(), 1u);
    EXPECT_TRUE(HasCycle(cycles, {"G1", "G2"}));
}

TEST(ArgumentCyclesTest, DetectsALongerCycle) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"),
                      Claim("G2"),
                      Claim("G3"),
                      Inference("R1", {"G2"}, "G1"),
                      Inference("R2", {"G3"}, "G2"),
                      Inference("R3", {"G1"}, "G3")};

    const std::vector<core::ArgumentCycle> cycles = core::FindSupportCycles(model);

    ASSERT_EQ(cycles.size(), 1u);
    EXPECT_TRUE(HasCycle(cycles, {"G1", "G2", "G3"}));
}

TEST(ArgumentCyclesTest, DetectsAnElementThatSupportsItself) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"), Inference("R1", {"G1"}, "G1")};

    const std::vector<core::ArgumentCycle> cycles = core::FindSupportCycles(model);

    ASSERT_EQ(cycles.size(), 1u);
    EXPECT_EQ(cycles.front().element_ids, std::vector<std::string>{"G1"});
}

// A strategy is a node in GSN, so the reported cycle must name it: it is
// usually the step the author has to break.
TEST(ArgumentCyclesTest, CycleThroughAStrategyNamesTheStrategy) {
    parser::AssuranceCase model;
    model.elements = {
        Claim("G1"), Claim("G2"), Strategy("S1"), Inference("R1", {"G2"}, "G1", "S1"), Inference("R2", {"G1"}, "G2")};

    const std::vector<core::ArgumentCycle> cycles = core::FindSupportCycles(model);

    ASSERT_EQ(cycles.size(), 1u);
    EXPECT_TRUE(HasCycle(cycles, {"G1", "S1", "G2"})) << "strategy missing from the reported cycle";
}

// InContextOf is not support. A context loop is a different (lesser) problem and
// must not be reported as a circular argument.
TEST(ArgumentCyclesTest, ContextRelationshipsAreNotSupport) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"), Claim("C1"), Context("R1", "C1", "G1"), Context("R2", "G1", "C1")};

    EXPECT_TRUE(core::FindSupportCycles(model).empty());
}

// A GSN v3 challenge points back at what it attacks. Treating it as support
// would report every dialectic argument in the case as circular.
TEST(ArgumentCyclesTest, ChallengesAreNotSupport) {
    // Counter-claim G9 attacks G1 while its own argument rests on G1. Reading
    // the challenge as support closes a G9 -> G1 -> G9 loop that is not there.
    parser::SacmElement challenge = Inference("CH1", {"G9"}, "G1");
    challenge.is_counter = true;

    parser::AssuranceCase model;
    model.elements = {Claim("G1"),
                      Claim("G9"),
                      Inference("R1", {"G1"}, "G9"), // G9 is supported by G1
                      challenge};                    // G9 challenges G1

    EXPECT_TRUE(core::FindSupportCycles(model).empty()) << "a challenge was followed as if it were support";

    // Sanity: the same shape *without* the counter flag really is a cycle, so
    // the assertion above is about `is_counter` and not about the fixture.
    parser::AssuranceCase without_counter = model;
    without_counter.elements.back().is_counter = false;
    EXPECT_EQ(core::FindSupportCycles(without_counter).size(), 1u);
}

TEST(ArgumentCyclesTest, ReportsEachDistinctCycleOnce) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"),
                      Claim("G2"),
                      Claim("G3"),
                      Claim("G4"),
                      // Two independent loops.
                      Inference("R1", {"G2"}, "G1"),
                      Inference("R2", {"G1"}, "G2"),
                      Inference("R3", {"G4"}, "G3"),
                      Inference("R4", {"G3"}, "G4")};

    const std::vector<core::ArgumentCycle> cycles = core::FindSupportCycles(model);

    ASSERT_EQ(cycles.size(), 2u);
    EXPECT_TRUE(HasCycle(cycles, {"G1", "G2"}));
    EXPECT_TRUE(HasCycle(cycles, {"G3", "G4"}));
}

TEST(ArgumentCyclesTest, DanglingReferencesDoNotCreatePhantomCycles) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"), Inference("R1", {"MISSING"}, "G1"), Inference("R2", {"G1"}, "ALSO_MISSING")};

    EXPECT_TRUE(core::FindSupportCycles(model).empty());
}

TEST(ArgumentCyclesTest, ResultIsIndependentOfDocumentOrder) {
    parser::AssuranceCase forward;
    forward.elements = {Claim("G1"),
                        Claim("G2"),
                        Claim("G3"),
                        Inference("R1", {"G2"}, "G1"),
                        Inference("R2", {"G3"}, "G2"),
                        Inference("R3", {"G1"}, "G3")};

    parser::AssuranceCase reversed;
    reversed.elements = {Inference("R3", {"G1"}, "G3"),
                         Inference("R2", {"G3"}, "G2"),
                         Inference("R1", {"G2"}, "G1"),
                         Claim("G3"),
                         Claim("G2"),
                         Claim("G1")};

    const std::vector<core::ArgumentCycle> a = core::FindSupportCycles(forward);
    const std::vector<core::ArgumentCycle> b = core::FindSupportCycles(reversed);

    ASSERT_EQ(a.size(), b.size());
    ASSERT_EQ(a.size(), 1u);
    EXPECT_EQ(a.front().element_ids, b.front().element_ids);
}

// A shared sub-goal makes the graph a DAG, not a tree. A naive visited-set walk
// reports the second path into it as a loop; this must not.
TEST(ArgumentCyclesTest, SharedSubGoalIsNotACycle) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"),
                      Claim("G2"),
                      Claim("G3"),
                      Claim("Gshared"),
                      Inference("R1", {"G2", "G3"}, "G1"),
                      Inference("R2", {"Gshared"}, "G2"),
                      Inference("R3", {"Gshared"}, "G3")};

    EXPECT_TRUE(core::FindSupportCycles(model).empty());
}

// ===== The app-level sync: detection only counts if a user sees it =====

TEST(StructureProblemSyncTest, ReportsACircularArgumentAsAnError) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"), Claim("G2"), Inference("R1", {"G2"}, "G1"), Inference("R2", {"G1"}, "G2")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);

    ASSERT_EQ(problems.GetProblems().size(), 1u);
    const core::ProblemItem& problem = problems.GetProblems().front();
    EXPECT_EQ(problem.severity, core::ProblemSeverity::Error);
    EXPECT_EQ(problem.type, "CircularArgument");
    EXPECT_EQ(problem.element_id, "G1");
    EXPECT_NE(problem.message.find("G1"), std::string::npos);
    EXPECT_NE(problem.message.find("G2"), std::string::npos);
}

TEST(StructureProblemSyncTest, ClearsTheProblemWhenTheCycleIsBroken) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"), Claim("G2"), Inference("R1", {"G2"}, "G1"), Inference("R2", {"G1"}, "G2")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);
    ASSERT_FALSE(problems.GetProblems().empty());

    model.elements.pop_back(); // break the loop
    app::SyncStructureProblems(problems, &model);

    EXPECT_TRUE(problems.GetProblems().empty()) << "a stale cycle problem outlived the cycle";
}

TEST(StructureProblemSyncTest, ResyncingDoesNotDuplicateProblems) {
    parser::AssuranceCase model;
    model.elements = {Claim("G1"), Claim("G2"), Inference("R1", {"G2"}, "G1"), Inference("R2", {"G1"}, "G2")};

    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, &model);
    app::SyncStructureProblems(problems, &model);
    app::SyncStructureProblems(problems, &model);

    EXPECT_EQ(problems.GetProblems().size(), 1u);
}

TEST(StructureProblemSyncTest, NoModelYieldsNoProblems) {
    core::ProblemsManager problems;
    app::SyncStructureProblems(problems, nullptr);
    EXPECT_TRUE(problems.GetProblems().empty());
}
