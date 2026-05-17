#pragma once

#include "parser/xml_parser.h"

#include <string>
#include <vector>

namespace core::acp {

enum class AcpRelationshipKind {
    SupportedBy,
    InContextOf,
    AssertedEvidence,
};

struct AcpRelationshipTarget {
    std::string relationship_id;
    AcpRelationshipKind kind = AcpRelationshipKind::SupportedBy;
    std::string parent_id;
    std::string child_id;
    std::string source_id;
    std::string target_id;
    std::string reasoning_id;
    bool strategy_child_edge = false;
    bool eligible_for_acp = true;
    std::string blocked_reason;
    std::string summary;
};

const char* ToString(AcpRelationshipKind kind);

std::vector<AcpRelationshipTarget> BuildAcpRelationshipTargets(const parser::AssuranceCase& model);
const AcpRelationshipTarget* FindAcpRelationshipTarget(const std::vector<AcpRelationshipTarget>& targets,
                                                       const std::string& parent_id,
                                                       const std::string& child_id);

} // namespace core::acp