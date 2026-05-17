#include "app/acp_problem_sync.h"

#include "core/acp/assurance_claim_point.h"
#include "core/problems/problems_manager.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

parser::SacmElement Element(std::string id, std::string type) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    return element;
}

bool HasProblemType(const core::ProblemsManager& manager, const std::string& type) {
    const auto& problems = manager.GetProblems();
    return std::any_of(
        problems.begin(), problems.end(), [&](const core::ProblemItem& problem) { return problem.type == type; });
}

} // namespace

TEST(AcpProblemSyncTest, ReportsUninstantiatedAndNonArtefactElementWarnings) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim"));
    parser::AcpRecord acp;
    acp.id = "ACP1";
    acp.target_kind = "element";
    acp.target_id = "G1";
    acp.resolution_kind = "none";
    model.acps.push_back(acp);

    core::ProblemsManager manager;
    app::SyncAcpProblems(manager, &model, nullptr);

    EXPECT_TRUE(HasProblemType(manager, "AcpUninstantiated"));
    EXPECT_TRUE(HasProblemType(manager, "AcpNonArtefactReferenceTarget"));
}

TEST(AcpProblemSyncTest, ReportsMissingTopGoalAndNonConfidencePackage) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("R1", "assertedinference"));
    parser::AcpRecord acp;
    acp.id = "ACP1";
    acp.target_kind = "relationship";
    acp.target_id = "R1";
    acp.resolution_kind = "topGoalReference";
    acp.argument_package_id = "AP1";
    acp.top_goal_id = "CC1";
    model.acps.push_back(acp);

    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    sacm::Claim top_goal;
    top_goal.id = "CC1";
    argument_package.claims.push_back(top_goal);
    package.argumentPackages.push_back(argument_package);

    core::ProblemsManager manager;
    app::SyncAcpProblems(manager, &model, &package);
    EXPECT_TRUE(HasProblemType(manager, "AcpNonConfidenceArgumentPackage"));
    EXPECT_FALSE(HasProblemType(manager, "AcpMissingTopGoal"));

    core::acp::SetConfidenceArgumentPackage(package.argumentPackages.front(), true);
    app::SyncAcpProblems(manager, &model, &package);
    EXPECT_FALSE(HasProblemType(manager, "AcpNonConfidenceArgumentPackage"));
    EXPECT_FALSE(HasProblemType(manager, "AcpMissingTopGoal"));

    package.argumentPackages.front().claims.clear();
    app::SyncAcpProblems(manager, &model, &package);
    EXPECT_TRUE(HasProblemType(manager, "AcpMissingTopGoal"));
}

TEST(AcpProblemSyncTest, ClearsPriorAcpProblemsByPrefix) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("Sn1", "artifactreference"));
    parser::AcpRecord acp;
    acp.id = "ACP1";
    acp.target_kind = "element";
    acp.target_id = "Sn1";
    acp.resolution_kind = "none";
    model.acps.push_back(acp);

    core::ProblemsManager manager;
    app::SyncAcpProblems(manager, &model, nullptr);
    EXPECT_TRUE(HasProblemType(manager, "AcpUninstantiated"));

    model.acps[0].resolution_kind = "text";
    model.acps[0].text = "Confidence argument.";
    app::SyncAcpProblems(manager, &model, nullptr);
    EXPECT_FALSE(HasProblemType(manager, "AcpUninstantiated"));
}
