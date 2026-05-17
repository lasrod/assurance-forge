#pragma once

#include "sacm/sacm_model.h"

#include <string>
#include <vector>

namespace core::acp {

enum class AcpTargetKind {
    Relationship,
    Element,
};

enum class AcpResolutionKind {
    None,
    Text,
    TopGoalReference,
};

struct AcpTarget {
    AcpTargetKind kind = AcpTargetKind::Element;
    std::string target_id;
};

struct AcpResolution {
    AcpResolutionKind kind = AcpResolutionKind::None;
    std::string text;
    std::string argument_package_id;
    std::string top_goal_id;
};

struct Acp {
    std::string id;
    AcpTarget target;
    AcpResolution resolution;
};

const char* ToString(AcpTargetKind kind);
const char* ToString(AcpResolutionKind kind);
AcpTargetKind AcpTargetKindFromString(const std::string& value);
AcpResolutionKind AcpResolutionKindFromString(const std::string& value);

bool IsInstantiated(const Acp& acp);

std::vector<Acp> AcpsForTaggedElement(const sacm::SacmElement& element, AcpTargetKind target_kind);
std::vector<Acp> CollectAcps(const sacm::ArgumentPackage& package);
std::vector<Acp> CollectAcps(const sacm::AssuranceCasePackage& package);
std::string NextAcpId(const std::vector<Acp>& existing_acps);
std::string NextAcpId(const sacm::ArgumentPackage& package);

void UpsertAcpTags(sacm::SacmElement& element, const Acp& acp);
bool RemoveAcpTags(sacm::SacmElement& element, const std::string& acp_id);

bool IsConfidenceArgumentPackage(const sacm::ArgumentPackage& package);
void SetConfidenceArgumentPackage(sacm::ArgumentPackage& package, bool confidence_argument);

} // namespace core::acp