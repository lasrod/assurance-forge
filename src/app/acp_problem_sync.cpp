#include "app/acp_problem_sync.h"

#include "core/acp/acp_relationship_index.h"
#include "core/acp/assurance_claim_point.h"
#include "core/problems/problem_utils.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace app {
namespace {

constexpr const char* kAcpProblemPrefix = "acp:";

bool IsRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == id;
    });
    return found == model.elements.end() ? nullptr : &*found;
}

const sacm::ArgumentPackage* FindArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                 const std::string& package_id) {
    auto found =
        std::find_if(package.argumentPackages.begin(),
                     package.argumentPackages.end(),
                     [&](const sacm::ArgumentPackage& argument_package) { return argument_package.id == package_id; });
    return found == package.argumentPackages.end() ? nullptr : &*found;
}

const sacm::Claim* FindClaim(const sacm::ArgumentPackage& package, const std::string& claim_id) {
    auto found = std::find_if(
        package.claims.begin(), package.claims.end(), [&](const sacm::Claim& claim) { return claim.id == claim_id; });
    return found == package.claims.end() ? nullptr : &*found;
}

bool TopGoalExists(const sacm::AssuranceCasePackage& package,
                   const std::string& argument_package_id,
                   const std::string& top_goal_id) {
    if (top_goal_id.empty())
        return false;
    if (!argument_package_id.empty()) {
        const sacm::ArgumentPackage* argument_package = FindArgumentPackage(package, argument_package_id);
        return argument_package && FindClaim(*argument_package, top_goal_id);
    }
    return std::any_of(package.argumentPackages.begin(),
                       package.argumentPackages.end(),
                       [&](const sacm::ArgumentPackage& argument_package) {
                           return FindClaim(argument_package, top_goal_id) != nullptr;
                       });
}

bool PackageIsConfidenceArgument(const sacm::AssuranceCasePackage& package, const std::string& argument_package_id) {
    const sacm::ArgumentPackage* argument_package = FindArgumentPackage(package, argument_package_id);
    return argument_package && core::acp::IsConfidenceArgumentPackage(*argument_package);
}

bool ElementEligibleForAcp(const parser::SacmElement& element) {
    return element.type == "artifactreference";
}

bool RelationshipEligibleForAcp(const parser::AssuranceCase& model, const std::string& relationship_id) {
    const std::vector<core::acp::AcpRelationshipTarget> targets = core::acp::BuildAcpRelationshipTargets(model);
    return std::any_of(targets.begin(), targets.end(), [&](const core::acp::AcpRelationshipTarget& target) {
        return target.relationship_id == relationship_id && target.eligible_for_acp;
    });
}

core::ProblemItem
MakeProblem(const parser::AcpRecord& acp, core::ProblemSeverity severity, std::string code, std::string message) {
    core::ProblemItem problem;
    problem.id = std::string(kAcpProblemPrefix) + acp.id + ":" + code;
    problem.severity = severity;
    problem.source = core::ProblemSource::ModelValidation;
    problem.element_id = acp.target_id;
    problem.type = "Acp" + code;
    problem.message = std::move(message);
    problem.quick_fix_label = "Open ACP";
    problem.quick_fix_payload = acp.id;
    return problem;
}

} // namespace

void SyncAcpProblems(core::ProblemsManager& problems_manager,
                     const parser::AssuranceCase* model,
                     const sacm::AssuranceCasePackage* package) {
    core::ClearProblemsByIdPrefix(problems_manager, kAcpProblemPrefix);
    if (!model)
        return;

    std::unordered_map<std::string, int> id_counts;
    for (const parser::AcpRecord& acp : model->acps)
        ++id_counts[acp.id];

    for (const parser::AcpRecord& acp : model->acps) {
        const parser::SacmElement* target = FindElement(*model, acp.target_id);

        if (id_counts[acp.id] > 1) {
            problems_manager.AddOrUpdateProblem(
                MakeProblem(acp,
                            core::ProblemSeverity::Error,
                            "DuplicateId",
                            "ACP id " + acp.id + " is duplicated in this argument package."));
        }

        if (!target) {
            problems_manager.AddOrUpdateProblem(
                MakeProblem(acp,
                            core::ProblemSeverity::Error,
                            "MissingTarget",
                            "ACP " + acp.id + " targets an element or relationship that no longer exists."));
            continue;
        }

        if (acp.target_kind == "relationship" && !IsRelationshipType(target->type)) {
            problems_manager.AddOrUpdateProblem(MakeProblem(
                acp,
                core::ProblemSeverity::Error,
                "WrongTargetKind",
                "ACP " + acp.id + " is marked as a relationship ACP but targets a non-relationship element."));
        } else if (acp.target_kind == "element" && IsRelationshipType(target->type)) {
            problems_manager.AddOrUpdateProblem(
                MakeProblem(acp,
                            core::ProblemSeverity::Error,
                            "WrongTargetKind",
                            "ACP " + acp.id + " is marked as an element ACP but targets a relationship."));
        }

        if (acp.resolution_kind != "text" && acp.resolution_kind != "topGoalReference") {
            problems_manager.AddOrUpdateProblem(MakeProblem(
                acp,
                core::ProblemSeverity::Warning,
                "Uninstantiated",
                "ACP " + acp.id + " is uninstantiated. Add a text confidence argument or link a confidence top goal."));
        }

        if (acp.target_kind == "element" && !ElementEligibleForAcp(*target)) {
            problems_manager.AddOrUpdateProblem(
                MakeProblem(acp,
                            core::ProblemSeverity::Warning,
                            "NonArtefactReferenceTarget",
                            "ACP " + acp.id + " is attached to an element that is not an artefact reference."));
        }

        if (acp.target_kind == "relationship" && IsRelationshipType(target->type) &&
            !RelationshipEligibleForAcp(*model, acp.target_id)) {
            problems_manager.AddOrUpdateProblem(
                MakeProblem(acp,
                            core::ProblemSeverity::Warning,
                            "IneligibleRelationshipTarget",
                            "ACP " + acp.id + " is attached to a relationship that is not eligible for ACP."));
        }

        if (acp.resolution_kind == "text") {
            if (acp.text.empty()) {
                problems_manager.AddOrUpdateProblem(MakeProblem(
                    acp,
                    core::ProblemSeverity::Warning,
                    "MissingText",
                    "ACP " + acp.id + " is set to 'text confidence argument' but no text has been provided."));
            }
        }

        if (acp.resolution_kind == "topGoalReference") {
            if (acp.argument_package_id.empty() || acp.top_goal_id.empty()) {
                problems_manager.AddOrUpdateProblem(MakeProblem(
                    acp,
                    core::ProblemSeverity::Warning,
                    "MissingTopGoal",
                    "ACP " + acp.id + " is set to 'separate confidence argument tree' but is not yet linked to one."));
            } else if (!package || !TopGoalExists(*package, acp.argument_package_id, acp.top_goal_id)) {
                problems_manager.AddOrUpdateProblem(
                    MakeProblem(acp,
                                core::ProblemSeverity::Error,
                                "MissingTopGoal",
                                "ACP " + acp.id + " links to a confidence top goal that no longer exists."));
            } else if (!acp.argument_package_id.empty() &&
                       !PackageIsConfidenceArgument(*package, acp.argument_package_id)) {
                problems_manager.AddOrUpdateProblem(MakeProblem(
                    acp,
                    core::ProblemSeverity::Warning,
                    "NonConfidenceArgumentPackage",
                    "ACP " + acp.id + " links to a package that is not marked as a confidence argument package."));
            }
        }
    }
}

} // namespace app