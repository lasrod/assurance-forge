#include "core/acp/assurance_claim_point.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace core::acp {
namespace {

constexpr const char* kAcpMarkerKey = "assuranceForge.acp";
constexpr const char* kAcpFieldPrefix = "assuranceForge.acp.";
constexpr const char* kResolutionKindField = ".resolutionKind";
constexpr const char* kTextField = ".text";
constexpr const char* kArgumentPackageIdField = ".argumentPackageId";
constexpr const char* kTopGoalIdField = ".topGoalId";
constexpr const char* kPackagePurposeKey = "assuranceForge.argumentPackage.purpose";
constexpr const char* kPackagePurposeConfidence = "confidence";

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

std::string FieldKey(const std::string& acp_id, const char* field) {
    return std::string(kAcpFieldPrefix) + acp_id + field;
}

bool IsAcpTagForId(const sacm::TaggedValue& tag, const std::string& acp_id) {
    if (tag.key == kAcpMarkerKey && tag.value == acp_id)
        return true;
    const std::string prefix = std::string(kAcpFieldPrefix) + acp_id + ".";
    return tag.key.rfind(prefix, 0) == 0;
}

std::string TaggedValueFor(const sacm::SacmElement& element, const std::string& key) {
    for (const sacm::TaggedValue& tag : element.taggedValues) {
        if (tag.key == key)
            return tag.value;
    }
    return {};
}

void AddTag(std::vector<sacm::TaggedValue>& tags, std::string key, std::string value, std::string id = {}) {
    sacm::TaggedValue tag;
    tag.id = std::move(id);
    tag.key = std::move(key);
    tag.value = std::move(value);
    tags.push_back(std::move(tag));
}

void CollectFromElement(const sacm::SacmElement& element, AcpTargetKind target_kind, std::vector<Acp>& out_acps) {
    std::unordered_set<std::string> seen_ids;
    for (const sacm::TaggedValue& tag : element.taggedValues) {
        if (tag.key != kAcpMarkerKey || tag.value.empty())
            continue;
        if (!seen_ids.insert(tag.value).second)
            continue;

        Acp acp;
        acp.id = tag.value;
        acp.target.kind = target_kind;
        acp.target.target_id = element.id;
        acp.resolution.kind =
            AcpResolutionKindFromString(TaggedValueFor(element, FieldKey(acp.id, kResolutionKindField)));
        acp.resolution.text = TaggedValueFor(element, FieldKey(acp.id, kTextField));
        acp.resolution.argument_package_id = TaggedValueFor(element, FieldKey(acp.id, kArgumentPackageIdField));
        acp.resolution.top_goal_id = TaggedValueFor(element, FieldKey(acp.id, kTopGoalIdField));
        out_acps.push_back(std::move(acp));
    }
}

void CollectFromPackage(const sacm::ArgumentPackage& package, std::vector<Acp>& out_acps) {
    for (const sacm::Claim& claim : package.claims)
        CollectFromElement(claim, AcpTargetKind::Element, out_acps);
    for (const sacm::ArgumentReasoning& reasoning : package.argumentReasonings)
        CollectFromElement(reasoning, AcpTargetKind::Element, out_acps);
    for (const sacm::ArtifactReference& reference : package.artifactReferences)
        CollectFromElement(reference, AcpTargetKind::Element, out_acps);
    for (const sacm::AssertedInference& inference : package.assertedInferences)
        CollectFromElement(inference, AcpTargetKind::Relationship, out_acps);
    for (const sacm::AssertedContext& context : package.assertedContexts)
        CollectFromElement(context, AcpTargetKind::Relationship, out_acps);
    for (const sacm::AssertedEvidence& evidence : package.assertedEvidences)
        CollectFromElement(evidence, AcpTargetKind::Relationship, out_acps);
}

} // namespace

const char* ToString(AcpTargetKind kind) {
    switch (kind) {
    case AcpTargetKind::Relationship:
        return "relationship";
    case AcpTargetKind::Element:
        return "element";
    }
    return "element";
}

const char* ToString(AcpResolutionKind kind) {
    switch (kind) {
    case AcpResolutionKind::None:
        return "none";
    case AcpResolutionKind::Text:
        return "text";
    case AcpResolutionKind::TopGoalReference:
        return "topGoalReference";
    }
    return "none";
}

AcpTargetKind AcpTargetKindFromString(const std::string& value) {
    const std::string normalized = ToLower(value);
    if (normalized == "relationship")
        return AcpTargetKind::Relationship;
    return AcpTargetKind::Element;
}

AcpResolutionKind AcpResolutionKindFromString(const std::string& value) {
    const std::string normalized = ToLower(value);
    if (normalized == "text")
        return AcpResolutionKind::Text;
    if (normalized == "topgoalreference")
        return AcpResolutionKind::TopGoalReference;
    return AcpResolutionKind::None;
}

bool IsInstantiated(const Acp& acp) {
    return acp.resolution.kind == AcpResolutionKind::Text || acp.resolution.kind == AcpResolutionKind::TopGoalReference;
}

std::vector<Acp> AcpsForTaggedElement(const sacm::SacmElement& element, AcpTargetKind target_kind) {
    std::vector<Acp> acps;
    CollectFromElement(element, target_kind, acps);
    return acps;
}

std::vector<Acp> CollectAcps(const sacm::ArgumentPackage& package) {
    std::vector<Acp> acps;
    CollectFromPackage(package, acps);
    return acps;
}

std::vector<Acp> CollectAcps(const sacm::AssuranceCasePackage& package) {
    std::vector<Acp> acps;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages)
        CollectFromPackage(argument_package, acps);
    return acps;
}

std::string NextAcpId(const std::vector<Acp>& existing_acps) {
    std::unordered_set<std::string> existing_ids;
    for (const Acp& acp : existing_acps) {
        if (!acp.id.empty())
            existing_ids.insert(acp.id);
    }
    for (int index = 1; index < 100000; ++index) {
        std::string candidate = "ACP" + std::to_string(index);
        if (existing_ids.find(candidate) == existing_ids.end())
            return candidate;
    }
    return "ACPx";
}

std::string NextAcpId(const sacm::ArgumentPackage& package) {
    return NextAcpId(CollectAcps(package));
}

void UpsertAcpTags(sacm::SacmElement& element, const Acp& acp) {
    RemoveAcpTags(element, acp.id);
    if (acp.id.empty())
        return;

    AddTag(element.taggedValues, std::string(kAcpMarkerKey), acp.id, acp.id);
    AddTag(element.taggedValues, FieldKey(acp.id, kResolutionKindField), ToString(acp.resolution.kind));
    if (acp.resolution.kind == AcpResolutionKind::Text) {
        AddTag(element.taggedValues, FieldKey(acp.id, kTextField), acp.resolution.text);
    } else if (acp.resolution.kind == AcpResolutionKind::TopGoalReference) {
        AddTag(element.taggedValues, FieldKey(acp.id, kArgumentPackageIdField), acp.resolution.argument_package_id);
        AddTag(element.taggedValues, FieldKey(acp.id, kTopGoalIdField), acp.resolution.top_goal_id);
    }
}

bool RemoveAcpTags(sacm::SacmElement& element, const std::string& acp_id) {
    const auto original_size = element.taggedValues.size();
    element.taggedValues.erase(std::remove_if(element.taggedValues.begin(),
                                              element.taggedValues.end(),
                                              [&](const sacm::TaggedValue& tag) { return IsAcpTagForId(tag, acp_id); }),
                               element.taggedValues.end());
    return element.taggedValues.size() != original_size;
}

bool IsConfidenceArgumentPackage(const sacm::ArgumentPackage& package) {
    for (const sacm::TaggedValue& tag : package.taggedValues) {
        if (tag.key == kPackagePurposeKey && ToLower(tag.value) == kPackagePurposeConfidence)
            return true;
    }
    return false;
}

void SetConfidenceArgumentPackage(sacm::ArgumentPackage& package, bool confidence_argument) {
    package.taggedValues.erase(
        std::remove_if(package.taggedValues.begin(),
                       package.taggedValues.end(),
                       [](const sacm::TaggedValue& tag) { return tag.key == kPackagePurposeKey; }),
        package.taggedValues.end());
    if (confidence_argument)
        AddTag(package.taggedValues, std::string(kPackagePurposeKey), std::string(kPackagePurposeConfidence));
}

} // namespace core::acp