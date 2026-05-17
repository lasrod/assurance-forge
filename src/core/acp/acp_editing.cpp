#include "core/acp/acp_editing.h"

#include "core/acp/acp_relationship_index.h"
#include "core/acp/assurance_claim_point.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace core::acp {
namespace {

constexpr const char* kTargetKindElement = "element";
constexpr const char* kTargetKindRelationship = "relationship";

struct SacmTargetRef {
    sacm::ArgumentPackage* owning_package = nullptr;
    sacm::SacmElement* element = nullptr;
};

AcpEditResult ErrorResult(std::string acp_id, std::string error) {
    AcpEditResult result;
    result.acp_id = std::move(acp_id);
    result.error = std::move(error);
    return result;
}

AcpEditResult SuccessResult(std::string acp_id,
                            std::string argument_package_id = {},
                            std::string top_goal_id = {}) {
    AcpEditResult result;
    result.changed = true;
    result.acp_id = std::move(acp_id);
    result.argument_package_id = std::move(argument_package_id);
    result.top_goal_id = std::move(top_goal_id);
    return result;
}

template <typename ElementT>
sacm::SacmElement* FindById(std::vector<ElementT>& elements, const std::string& id) {
    auto found = std::find_if(elements.begin(), elements.end(), [&](const ElementT& element) {
        return element.id == id;
    });
    return found == elements.end() ? nullptr : &*found;
}

SacmTargetRef FindSacmTarget(sacm::AssuranceCasePackage* package,
                             const std::string& target_kind,
                             const std::string& target_id) {
    if (!package || target_id.empty())
        return {};

    for (sacm::ArgumentPackage& argument_package : package->argumentPackages) {
        sacm::SacmElement* element = nullptr;
        if (target_kind == kTargetKindRelationship) {
            element = FindById(argument_package.assertedInferences, target_id);
            if (!element)
                element = FindById(argument_package.assertedContexts, target_id);
            if (!element)
                element = FindById(argument_package.assertedEvidences, target_id);
        } else {
            element = FindById(argument_package.claims, target_id);
            if (!element)
                element = FindById(argument_package.argumentReasonings, target_id);
            if (!element)
                element = FindById(argument_package.artifactReferences, target_id);
        }
        if (element)
            return SacmTargetRef{&argument_package, element};
    }
    return {};
}

bool ParserTargetExists(const parser::AssuranceCase& model,
                        const std::string& target_kind,
                        const std::string& target_id) {
    if (target_id.empty())
        return false;
    for (const parser::SacmElement& element : model.elements) {
        if (element.id != target_id)
            continue;
        if (target_kind == kTargetKindRelationship) {
            return element.type == "assertedinference" || element.type == "assertedcontext" ||
                   element.type == "assertedevidence";
        }
        return element.type != "assertedinference" && element.type != "assertedcontext" &&
               element.type != "assertedevidence";
    }
    return false;
}

const parser::SacmElement* FindParserElement(const parser::AssuranceCase& model, const std::string& element_id) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == element_id;
    });
    return found == model.elements.end() ? nullptr : &*found;
}

bool ElementEligibleForAcp(const parser::AssuranceCase& model, const std::string& element_id) {
    const parser::SacmElement* element = FindParserElement(model, element_id);
    return element && element->type == "artifactreference";
}

bool RelationshipEligibleForAcp(const parser::AssuranceCase& model, const std::string& relationship_id) {
    const std::vector<AcpRelationshipTarget> targets = BuildAcpRelationshipTargets(model);
    return std::any_of(targets.begin(), targets.end(), [&](const AcpRelationshipTarget& target) {
        return target.relationship_id == relationship_id && target.eligible_for_acp;
    });
}

std::string IneligibleTargetMessage(const std::string& target_kind) {
    if (target_kind == kTargetKindElement) {
        return "Element ACP is only allowed on artefact references, such as Solutions or artefact-backed Contexts.";
    }
    return "Relationship ACP is only allowed on eligible SupportedBy or InContextOf relationships.";
}

parser::AcpRecord ToRecord(const Acp& acp) {
    parser::AcpRecord record;
    record.id = acp.id;
    record.target_kind = ToString(acp.target.kind);
    record.target_id = acp.target.target_id;
    record.resolution_kind = ToString(acp.resolution.kind);
    record.text = acp.resolution.text;
    record.argument_package_id = acp.resolution.argument_package_id;
    record.top_goal_id = acp.resolution.top_goal_id;
    return record;
}

Acp ToDomain(const parser::AcpRecord& record) {
    Acp acp;
    acp.id = record.id;
    acp.target.kind = AcpTargetKindFromString(record.target_kind);
    acp.target.target_id = record.target_id;
    acp.resolution.kind = AcpResolutionKindFromString(record.resolution_kind);
    acp.resolution.text = record.text;
    acp.resolution.argument_package_id = record.argument_package_id;
    acp.resolution.top_goal_id = record.top_goal_id;
    return acp;
}

bool UpsertParserRecord(parser::AssuranceCase& model, const parser::AcpRecord& record) {
    parser::AcpRecord* existing = FindAcp(model, record.id);
    if (existing) {
        *existing = record;
        return true;
    }
    model.acps.push_back(record);
    return true;
}

std::vector<Acp> CollectAcpsForIdGeneration(const parser::AssuranceCase& model, const sacm::ArgumentPackage* package) {
    if (package)
        return CollectAcps(*package);
    std::vector<Acp> acps;
    for (const parser::AcpRecord& record : model.acps)
        acps.push_back(ToDomain(record));
    return acps;
}

bool RelationshipAlreadyHasAcp(const parser::AssuranceCase& model,
                               const std::string& relationship_id,
                               const std::string& except_acp_id = {}) {
    return std::any_of(model.acps.begin(), model.acps.end(), [&](const parser::AcpRecord& acp) {
        return acp.target_kind == kTargetKindRelationship && acp.target_id == relationship_id &&
               acp.id != except_acp_id;
    });
}

int NumericSuffix(const std::string& value, const std::string& prefix) {
    if (value.rfind(prefix, 0) != 0 || value.size() == prefix.size())
        return 0;
    int number = 0;
    for (std::size_t index = prefix.size(); index < value.size(); ++index) {
        unsigned char ch = static_cast<unsigned char>(value[index]);
        if (!std::isdigit(ch))
            return 0;
        number = number * 10 + (value[index] - '0');
    }
    return number;
}

std::string NextArgumentPackageId(const sacm::AssuranceCasePackage& package) {
    int max_number = 0;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages)
        max_number = std::max(max_number, NumericSuffix(argument_package.id, "AP"));
    return "AP" + std::to_string(max_number + 1);
}

std::string NextTopGoalId(const parser::AssuranceCase& model, const sacm::AssuranceCasePackage& package) {
    int max_number = 0;
    for (const parser::SacmElement& element : model.elements)
        max_number = std::max(max_number, NumericSuffix(element.id, "CC"));
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::Claim& claim : argument_package.claims)
            max_number = std::max(max_number, NumericSuffix(claim.id, "CC"));
    }
    return "CC" + std::to_string(max_number + 1);
}

std::string TargetSummaryForDefaultClaim(const parser::AssuranceCase& model, const parser::AcpRecord& acp) {
    if (acp.target_kind == kTargetKindRelationship) {
        const std::vector<AcpRelationshipTarget> targets = BuildAcpRelationshipTargets(model);
        auto found = std::find_if(targets.begin(), targets.end(), [&](const AcpRelationshipTarget& target) {
            return target.relationship_id == acp.target_id && target.eligible_for_acp;
        });
        if (found != targets.end())
            return found->summary;
    }
    return acp.target_kind + " " + acp.target_id;
}

} // namespace

const parser::AcpRecord* FindAcp(const parser::AssuranceCase& model, const std::string& acp_id) {
    auto found = std::find_if(model.acps.begin(), model.acps.end(), [&](const parser::AcpRecord& acp) {
        return acp.id == acp_id;
    });
    return found == model.acps.end() ? nullptr : &*found;
}

parser::AcpRecord* FindAcp(parser::AssuranceCase& model, const std::string& acp_id) {
    auto found = std::find_if(model.acps.begin(), model.acps.end(), [&](const parser::AcpRecord& acp) {
        return acp.id == acp_id;
    });
    return found == model.acps.end() ? nullptr : &*found;
}

AcpEditResult AddAcp(parser::AssuranceCase& model,
                     sacm::AssuranceCasePackage* package,
                     const std::string& target_kind,
                     const std::string& target_id) {
    if (target_kind != kTargetKindElement && target_kind != kTargetKindRelationship)
        return ErrorResult({}, "Unsupported ACP target kind.");
    if (!ParserTargetExists(model, target_kind, target_id))
        return ErrorResult({}, "ACP target was not found in the active model.");
    if (target_kind == kTargetKindElement && !ElementEligibleForAcp(model, target_id))
        return ErrorResult({}, IneligibleTargetMessage(target_kind));
    if (target_kind == kTargetKindRelationship && !RelationshipEligibleForAcp(model, target_id))
        return ErrorResult({}, IneligibleTargetMessage(target_kind));
    if (target_kind == kTargetKindRelationship && RelationshipAlreadyHasAcp(model, target_id))
        return ErrorResult({}, "This relationship already has an ACP.");

    SacmTargetRef target = FindSacmTarget(package, target_kind, target_id);
    if (!target.element)
        return ErrorResult({}, "ACP target was not found in the SACM package.");

    Acp acp;
    acp.id = NextAcpId(CollectAcpsForIdGeneration(model, target.owning_package));
    acp.target.kind = AcpTargetKindFromString(target_kind);
    acp.target.target_id = target_id;
    acp.resolution.kind = AcpResolutionKind::None;

    UpsertAcpTags(*target.element, acp);
    UpsertParserRecord(model, ToRecord(acp));
    return SuccessResult(acp.id);
}

AcpEditResult UpsertAcp(parser::AssuranceCase& model,
                        sacm::AssuranceCasePackage* package,
                        const parser::AcpRecord& record) {
    if (record.id.empty())
        return ErrorResult({}, "ACP id is empty.");
    if (record.target_kind != kTargetKindElement && record.target_kind != kTargetKindRelationship)
        return ErrorResult(record.id, "Unsupported ACP target kind.");
    if (!ParserTargetExists(model, record.target_kind, record.target_id))
        return ErrorResult(record.id, "ACP target was not found in the active model.");
    if (record.target_kind == kTargetKindElement && !ElementEligibleForAcp(model, record.target_id))
        return ErrorResult(record.id, IneligibleTargetMessage(record.target_kind));
    if (record.target_kind == kTargetKindRelationship && !RelationshipEligibleForAcp(model, record.target_id))
        return ErrorResult(record.id, IneligibleTargetMessage(record.target_kind));
    if (record.target_kind == kTargetKindRelationship && RelationshipAlreadyHasAcp(model, record.target_id, record.id))
        return ErrorResult(record.id, "This relationship already has an ACP.");

    SacmTargetRef target = FindSacmTarget(package, record.target_kind, record.target_id);
    if (!target.element)
        return ErrorResult(record.id, "ACP target was not found in the SACM package.");

    UpsertAcpTags(*target.element, ToDomain(record));
    UpsertParserRecord(model, record);
    return SuccessResult(record.id);
}

AcpEditResult RemoveAcp(parser::AssuranceCase& model,
                        sacm::AssuranceCasePackage* package,
                        const std::string& acp_id) {
    const parser::AcpRecord* existing = FindAcp(model, acp_id);
    if (!existing)
        return ErrorResult(acp_id, "ACP was not found.");

    SacmTargetRef target = FindSacmTarget(package, existing->target_kind, existing->target_id);
    if (!target.element)
        return ErrorResult(acp_id, "ACP target was not found in the SACM package.");

    RemoveAcpTags(*target.element, acp_id);
    model.acps.erase(std::remove_if(model.acps.begin(),
                                    model.acps.end(),
                                    [&](const parser::AcpRecord& acp) { return acp.id == acp_id; }),
                     model.acps.end());
    return SuccessResult(acp_id);
}

AcpEditResult CreateConfidenceArgumentTreeForAcp(parser::AssuranceCase& model,
                                                 sacm::AssuranceCasePackage* package,
                                                 const std::string& acp_id) {
    if (!package)
        return ErrorResult(acp_id, "No SACM package is loaded.");
    parser::AcpRecord* acp = FindAcp(model, acp_id);
    if (!acp)
        return ErrorResult(acp_id, "ACP was not found.");
    if (acp->resolution_kind == "topGoalReference" && !acp->argument_package_id.empty() && !acp->top_goal_id.empty())
        return ErrorResult(acp_id, "ACP already links to a confidence argument tree.");

    const std::string argument_package_id = NextArgumentPackageId(*package);
    const std::string top_goal_id = NextTopGoalId(model, *package);
    const std::string target_summary = TargetSummaryForDefaultClaim(model, *acp);
    const std::string top_goal_name = "Confidence argument for " + acp->id;
    const std::string top_goal_content = "Confidence in " + target_summary + " is sufficient.";

    sacm::ArgumentPackage argument_package;
    argument_package.id = argument_package_id;
    argument_package.name = top_goal_name;
    argument_package.name_ml.set("en", top_goal_name);
    SetConfidenceArgumentPackage(argument_package, true);

    sacm::Claim top_goal;
    top_goal.id = top_goal_id;
    top_goal.name = top_goal_name;
    top_goal.name_ml.set("en", top_goal_name);
    top_goal.content = top_goal_content;
    top_goal.content_ml.set("en", top_goal_content);
    top_goal.assertionDeclaration = "asserted";
    argument_package.claims.push_back(top_goal);
    package->argumentPackages.push_back(argument_package);

    parser::SacmElement parser_top_goal;
    parser_top_goal.id = top_goal_id;
    parser_top_goal.name = top_goal_name;
    parser_top_goal.type = "claim";
    parser_top_goal.content = top_goal_content;
    parser_top_goal.assertion_declaration = "asserted";
    parser_top_goal.name_langs["en"] = top_goal_name;
    parser_top_goal.content_langs["en"] = top_goal_content;
    model.elements.push_back(std::move(parser_top_goal));

    parser::AcpRecord updated = *acp;
    updated.resolution_kind = "topGoalReference";
    updated.text.clear();
    updated.argument_package_id = argument_package_id;
    updated.top_goal_id = top_goal_id;

    const AcpEditResult upsert_result = UpsertAcp(model, package, updated);
    if (!upsert_result.error.empty())
        return upsert_result;
    return SuccessResult(acp_id, argument_package_id, top_goal_id);
}

} // namespace core::acp