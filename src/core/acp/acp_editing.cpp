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

struct SacmRelationshipRef {
    sacm::ArgumentPackage* owning_package = nullptr;
    sacm::AssertedRelationship* relationship = nullptr;
};

AcpEditResult ErrorResult(std::string acp_id, std::string error) {
    AcpEditResult result;
    result.acp_id = std::move(acp_id);
    result.error = std::move(error);
    return result;
}

AcpEditResult SuccessResult(std::string acp_id, std::string argument_package_id = {}, std::string top_goal_id = {}) {
    AcpEditResult result;
    result.changed = true;
    result.acp_id = std::move(acp_id);
    result.argument_package_id = std::move(argument_package_id);
    result.top_goal_id = std::move(top_goal_id);
    return result;
}

template <typename ElementT>
sacm::SacmElement* FindById(std::vector<ElementT>& elements, const std::string& id) {
    auto found =
        std::find_if(elements.begin(), elements.end(), [&](const ElementT& element) { return element.id == id; });
    return found == elements.end() ? nullptr : &*found;
}

SacmTargetRef
FindSacmTarget(sacm::AssuranceCasePackage* package, const std::string& target_kind, const std::string& target_id) {
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

SacmRelationshipRef FindSacmRelationship(sacm::AssuranceCasePackage* package, const std::string& relationship_id) {
    if (!package || relationship_id.empty())
        return {};

    for (sacm::ArgumentPackage& argument_package : package->argumentPackages) {
        if (sacm::SacmElement* element = FindById(argument_package.assertedInferences, relationship_id))
            return SacmRelationshipRef{&argument_package, static_cast<sacm::AssertedRelationship*>(element)};
        if (sacm::SacmElement* element = FindById(argument_package.assertedContexts, relationship_id))
            return SacmRelationshipRef{&argument_package, static_cast<sacm::AssertedRelationship*>(element)};
        if (sacm::SacmElement* element = FindById(argument_package.assertedEvidences, relationship_id))
            return SacmRelationshipRef{&argument_package, static_cast<sacm::AssertedRelationship*>(element)};
    }
    return {};
}

void AddRefIfMissing(std::vector<std::string>& refs, const std::string& ref) {
    if (ref.empty())
        return;
    if (std::find(refs.begin(), refs.end(), ref) == refs.end())
        refs.push_back(ref);
}

void RemoveRef(std::vector<std::string>& refs, const std::string& ref) {
    if (ref.empty())
        return;
    refs.erase(std::remove(refs.begin(), refs.end(), ref), refs.end());
}

std::string RelationshipSemanticClaimId(const parser::AcpRecord& acp) {
    if (acp.target_kind != kTargetKindRelationship)
        return {};
    if (acp.resolution_kind == "topGoalReference")
        return acp.top_goal_id;
    if (acp.resolution_kind == "text")
        return acp.confidence_claim_id;
    return {};
}

void SyncRelationshipMetaClaim(sacm::AssuranceCasePackage* package,
                               const parser::AcpRecord& previous,
                               const parser::AcpRecord& current) {
    if (!package || current.target_kind != kTargetKindRelationship)
        return;

    const std::string previous_claim_id = RelationshipSemanticClaimId(previous);
    if (previous.target_kind == kTargetKindRelationship && previous.target_id != current.target_id) {
        SacmRelationshipRef previous_relationship = FindSacmRelationship(package, previous.target_id);
        if (previous_relationship.relationship)
            RemoveRef(previous_relationship.relationship->metaClaims, previous_claim_id);
    }

    SacmRelationshipRef relationship = FindSacmRelationship(package, current.target_id);
    if (!relationship.relationship)
        return;

    const std::string current_claim_id = RelationshipSemanticClaimId(current);
    if (previous.target_id == current.target_id && previous_claim_id != current_claim_id)
        RemoveRef(relationship.relationship->metaClaims, previous_claim_id);
    AddRefIfMissing(relationship.relationship->metaClaims, current_claim_id);
}

void ClearRelationshipMetaClaim(sacm::AssuranceCasePackage* package, const parser::AcpRecord& acp) {
    if (!package || acp.target_kind != kTargetKindRelationship)
        return;
    SacmRelationshipRef relationship = FindSacmRelationship(package, acp.target_id);
    if (!relationship.relationship)
        return;
    RemoveRef(relationship.relationship->metaClaims, RelationshipSemanticClaimId(acp));
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
    record.name = acp.name;
    record.target_kind = ToString(acp.target.kind);
    record.target_id = acp.target.target_id;
    record.resolution_kind = ToString(acp.resolution.kind);
    record.text = acp.resolution.text;
    record.confidence_claim_id = acp.resolution.confidence_claim_id;
    record.argument_package_id = acp.resolution.argument_package_id;
    record.top_goal_id = acp.resolution.top_goal_id;
    return record;
}

Acp ToDomain(const parser::AcpRecord& record) {
    Acp acp;
    acp.id = record.id;
    acp.name = record.name;
    acp.target.kind = AcpTargetKindFromString(record.target_kind);
    acp.target.target_id = record.target_id;
    acp.resolution.kind = AcpResolutionKindFromString(record.resolution_kind);
    acp.resolution.text = record.text;
    acp.resolution.confidence_claim_id = record.confidence_claim_id;
    acp.resolution.argument_package_id = record.argument_package_id;
    acp.resolution.top_goal_id = record.top_goal_id;
    return acp;
}

parser::AcpRecord NormalizeResolutionFields(parser::AcpRecord record) {
    const AcpResolutionKind kind = AcpResolutionKindFromString(record.resolution_kind);
    record.resolution_kind = ToString(kind);
    if (kind == AcpResolutionKind::None) {
        record.confidence_claim_id.clear();
        record.argument_package_id.clear();
        record.top_goal_id.clear();
    } else if (kind == AcpResolutionKind::Text) {
        record.argument_package_id.clear();
        record.top_goal_id.clear();
    } else if (kind == AcpResolutionKind::TopGoalReference) {
        record.confidence_claim_id.clear();
    }
    return record;
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

bool ArgumentPackageIdExists(const sacm::AssuranceCasePackage& package, const std::string& id) {
    return std::any_of(package.argumentPackages.begin(),
                       package.argumentPackages.end(),
                       [&](const auto& argument_package) { return argument_package.id == id; });
}

std::string NextAcpArgumentPackageId(const sacm::AssuranceCasePackage& package, const std::string& acp_id) {
    const std::string prefix = acp_id.empty() ? "ACP_AP" : acp_id + "_AP";
    if (!ArgumentPackageIdExists(package, prefix))
        return prefix;
    for (int index = 2; index < 100000; ++index) {
        const std::string candidate = prefix + std::to_string(index);
        if (!ArgumentPackageIdExists(package, candidate))
            return candidate;
    }
    return prefix + "x";
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

std::string NextElementIdWithPrefix(const parser::AssuranceCase& model,
                                    const sacm::AssuranceCasePackage& package,
                                    const std::string& prefix) {
    std::unordered_set<std::string> existing_ids;
    for (const parser::SacmElement& element : model.elements) {
        if (!element.id.empty())
            existing_ids.insert(element.id);
    }
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::Claim& claim : argument_package.claims)
            existing_ids.insert(claim.id);
        for (const sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings)
            existing_ids.insert(reasoning.id);
        for (const sacm::ArtifactReference& reference : argument_package.artifactReferences)
            existing_ids.insert(reference.id);
        for (const sacm::AssertedInference& inference : argument_package.assertedInferences)
            existing_ids.insert(inference.id);
        for (const sacm::AssertedContext& context : argument_package.assertedContexts)
            existing_ids.insert(context.id);
        for (const sacm::AssertedEvidence& evidence : argument_package.assertedEvidences)
            existing_ids.insert(evidence.id);
    }
    for (int index = 1; index < 100000; ++index) {
        const std::string candidate = prefix + std::to_string(index);
        if (existing_ids.find(candidate) == existing_ids.end())
            return candidate;
    }
    return prefix + "x";
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

sacm::Claim* FindClaim(sacm::AssuranceCasePackage& package, const std::string& claim_id) {
    if (claim_id.empty())
        return nullptr;
    for (sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        auto found = std::find_if(argument_package.claims.begin(),
                                  argument_package.claims.end(),
                                  [&](const sacm::Claim& claim) { return claim.id == claim_id; });
        if (found != argument_package.claims.end())
            return &*found;
    }
    return nullptr;
}

sacm::ArgumentPackage* PackageForTextConfidenceClaim(sacm::AssuranceCasePackage& package,
                                                     const parser::AcpRecord& record) {
    if (SacmTargetRef target = FindSacmTarget(&package, record.target_kind, record.target_id); target.owning_package)
        return target.owning_package;
    if (package.argumentPackages.empty())
        package.argumentPackages.emplace_back();
    return &package.argumentPackages.front();
}

void UpdateTextConfidenceClaimFields(sacm::Claim& claim, const parser::AcpRecord& acp) {
    const std::string claim_name = acp.name.empty() ? "Confidence argument for " + acp.id : acp.name;
    claim.name = claim_name;
    claim.name_ml.set("en", claim_name);
    claim.content = acp.text;
    claim.content_ml.set("en", acp.text);
    claim.assertionDeclaration = "asserted";
}

void UpdateTreeConfidenceNames(sacm::AssuranceCasePackage* package, const parser::AcpRecord& acp) {
    if (!package || acp.resolution_kind != "topGoalReference" || acp.argument_package_id.empty())
        return;
    auto package_found = std::find_if(
        package->argumentPackages.begin(),
        package->argumentPackages.end(),
        [&](const sacm::ArgumentPackage& argument_package) { return argument_package.id == acp.argument_package_id; });
    if (package_found == package->argumentPackages.end())
        return;

    const bool has_custom_display_name = !acp.name.empty() && acp.name != acp.id;
    if (!has_custom_display_name)
        return;

    const std::string native_name = acp.id + ": " + acp.name;
    package_found->name = native_name;
    package_found->name_ml.set("en", native_name);

    sacm::Claim* top_goal = FindClaim(*package, acp.top_goal_id);
    if (top_goal) {
        top_goal->name = native_name;
        top_goal->name_ml.set("en", native_name);
    }
}

parser::AcpRecord
EnsureTextConfidenceClaim(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package, parser::AcpRecord record) {
    if (!package || record.resolution_kind != "text")
        return record;

    if (record.confidence_claim_id.empty())
        record.confidence_claim_id = NextTopGoalId(model, *package);

    sacm::Claim* claim = FindClaim(*package, record.confidence_claim_id);
    if (!claim) {
        sacm::ArgumentPackage* argument_package = PackageForTextConfidenceClaim(*package, record);
        sacm::Claim generated_claim;
        generated_claim.id = record.confidence_claim_id;
        UpdateTextConfidenceClaimFields(generated_claim, record);
        argument_package->claims.push_back(std::move(generated_claim));
        claim = FindClaim(*package, record.confidence_claim_id);
    }

    if (claim) {
        UpdateTextConfidenceClaimFields(*claim, record);
    }
    return record;
}

} // namespace

const parser::AcpRecord* FindAcp(const parser::AssuranceCase& model, const std::string& acp_id) {
    auto found = std::find_if(
        model.acps.begin(), model.acps.end(), [&](const parser::AcpRecord& acp) { return acp.id == acp_id; });
    return found == model.acps.end() ? nullptr : &*found;
}

parser::AcpRecord* FindAcp(parser::AssuranceCase& model, const std::string& acp_id) {
    auto found = std::find_if(
        model.acps.begin(), model.acps.end(), [&](const parser::AcpRecord& acp) { return acp.id == acp_id; });
    return found == model.acps.end() ? nullptr : &*found;
}

AcpEditResult AddAcp(parser::AssuranceCase& model,
                     sacm::AssuranceCasePackage* package,
                     const std::string& target_kind,
                     const std::string& target_id) {
    // Generate the id from the full flat model (all packages) to avoid colliding with ACP ids that
    // already exist in other argument packages, then delegate. Audit replay calls AddAcpWithId
    // directly with the id recorded here.
    const std::string acp_id = NextAcpId(CollectAcpsForIdGeneration(model, nullptr));
    return AddAcpWithId(model, package, target_kind, target_id, acp_id);
}

AcpEditResult AddAcpWithId(parser::AssuranceCase& model,
                           sacm::AssuranceCasePackage* package,
                           const std::string& target_kind,
                           const std::string& target_id,
                           const std::string& acp_id) {
    // A caller-supplied id (audit replay, direct callers) must be non-empty: an
    // empty id would write an unaddressable ACP tag set and corrupt the model.
    if (acp_id.empty())
        return ErrorResult({}, "ACP id must not be empty.");
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
    acp.id = acp_id;
    acp.name = acp.id;
    acp.target.kind = AcpTargetKindFromString(target_kind);
    acp.target.target_id = target_id;
    acp.resolution.kind = AcpResolutionKind::None;

    UpsertAcpTags(*target.element, acp);
    UpsertParserRecord(model, ToRecord(acp));
    return SuccessResult(acp.id);
}

AcpEditResult
UpsertAcp(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package, const parser::AcpRecord& record) {
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

    parser::AcpRecord previous;
    if (const parser::AcpRecord* existing = FindAcp(model, record.id))
        previous = *existing;
    parser::AcpRecord normalized = NormalizeResolutionFields(record);
    normalized = EnsureTextConfidenceClaim(model, package, normalized);

    target = FindSacmTarget(package, normalized.target_kind, normalized.target_id);
    if (!target.element)
        return ErrorResult(record.id, "ACP target was not found in the SACM package.");

    // If the upsert retargets an existing ACP to a different SACM element/relationship, strip the
    // ACP tags from the previous target so the saved SACM does not leak a stale duplicate.
    if (!previous.id.empty() &&
        (previous.target_kind != normalized.target_kind || previous.target_id != normalized.target_id)) {
        SacmTargetRef previous_target = FindSacmTarget(package, previous.target_kind, previous.target_id);
        if (previous_target.element)
            RemoveAcpTags(*previous_target.element, previous.id);
    }

    UpsertAcpTags(*target.element, ToDomain(normalized));
    SyncRelationshipMetaClaim(package, previous, normalized);
    UpdateTreeConfidenceNames(package, normalized);
    UpsertParserRecord(model, normalized);
    return SuccessResult(record.id);
}

AcpEditResult RemoveAcp(parser::AssuranceCase& model, sacm::AssuranceCasePackage* package, const std::string& acp_id) {
    const parser::AcpRecord* existing = FindAcp(model, acp_id);
    if (!existing)
        return ErrorResult(acp_id, "ACP was not found.");

    SacmTargetRef target = FindSacmTarget(package, existing->target_kind, existing->target_id);
    if (!target.element)
        return ErrorResult(acp_id, "ACP target was not found in the SACM package.");

    RemoveAcpTags(*target.element, acp_id);
    ClearRelationshipMetaClaim(package, *existing);
    std::erase_if(model.acps, [&](const parser::AcpRecord& acp) { return acp.id == acp_id; });
    return SuccessResult(acp_id);
}

AcpEditResult CreateConfidenceArgumentTreeForAcp(parser::AssuranceCase& model,
                                                 sacm::AssuranceCasePackage* package,
                                                 const std::string& acp_id) {
    // Generate the two ids (the only non-deterministic outputs of this compound op)
    // and delegate. Audit replay calls CreateConfidenceArgumentTreeForAcpWithIds
    // directly with the ids recorded here.
    if (!package)
        return ErrorResult(acp_id, "No SACM package is loaded.");
    const parser::AcpRecord* acp = FindAcp(model, acp_id);
    if (!acp)
        return ErrorResult(acp_id, "ACP was not found.");
    const std::string argument_package_id = NextAcpArgumentPackageId(*package, acp_id);
    const std::string top_goal_id = NextElementIdWithPrefix(model, *package, acp_id + "_G");
    return CreateConfidenceArgumentTreeForAcpWithIds(model, package, acp_id, argument_package_id, top_goal_id);
}

AcpEditResult CreateConfidenceArgumentTreeForAcpWithIds(parser::AssuranceCase& model,
                                                        sacm::AssuranceCasePackage* package,
                                                        const std::string& acp_id,
                                                        const std::string& argument_package_id,
                                                        const std::string& top_goal_id) {
    if (!package)
        return ErrorResult(acp_id, "No SACM package is loaded.");
    if (argument_package_id.empty() || top_goal_id.empty())
        return ErrorResult(acp_id, "Confidence argument tree ids must not be empty.");
    parser::AcpRecord* acp = FindAcp(model, acp_id);
    if (!acp)
        return ErrorResult(acp_id, "ACP was not found.");
    if (acp->resolution_kind == "topGoalReference" && !acp->argument_package_id.empty() && !acp->top_goal_id.empty())
        return ErrorResult(acp_id, "ACP already links to a confidence argument tree.");

    // Validate the ACP's target up front so the subsequent UpsertAcp call cannot fail after we have
    // already inserted the new argument package and parser top-goal projection below.
    if (acp->target_kind != kTargetKindElement && acp->target_kind != kTargetKindRelationship)
        return ErrorResult(acp_id, "Unsupported ACP target kind.");
    if (!ParserTargetExists(model, acp->target_kind, acp->target_id))
        return ErrorResult(acp_id, "ACP target was not found in the active model.");
    if (acp->target_kind == kTargetKindElement && !ElementEligibleForAcp(model, acp->target_id))
        return ErrorResult(acp_id, IneligibleTargetMessage(acp->target_kind));
    if (acp->target_kind == kTargetKindRelationship && !RelationshipEligibleForAcp(model, acp->target_id))
        return ErrorResult(acp_id, IneligibleTargetMessage(acp->target_kind));
    if (!FindSacmTarget(package, acp->target_kind, acp->target_id).element)
        return ErrorResult(acp_id, "ACP target was not found in the SACM package.");

    const std::string target_summary = TargetSummaryForDefaultClaim(model, *acp);
    const bool has_custom_display_name = !acp->name.empty() && acp->name != acp->id;
    const std::string top_goal_name = has_custom_display_name ? acp->id + ": " + acp->name : std::string{};
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
    updated.confidence_claim_id.clear();
    updated.argument_package_id = argument_package_id;
    updated.top_goal_id = top_goal_id;

    const AcpEditResult upsert_result = UpsertAcp(model, package, updated);
    if (!upsert_result.error.empty())
        return upsert_result;
    return SuccessResult(acp_id, argument_package_id, top_goal_id);
}

} // namespace core::acp