#pragma once

// The CSE register's columns, named once -- the same shape
// `core/evidence_attributes.h` gives the evidence register's, and for the same
// reason: the register, the audited command, the draft operation and the MCP
// wire all address a column by this token.

#include "core/sacm_model.h"

#include <array>
#include <string>
#include <string_view>

namespace core {

enum class CseAttribute {
    ClaimOwner,
    EvidenceOwner,
    SafetyCaseOwner,
    ClaimCriteria,
    EvidenceCriteria,
    AssessmentStatus,
    Notes,
};

inline constexpr std::array<CseAttribute, 7> kAllCseAttributes = {
    CseAttribute::ClaimOwner,
    CseAttribute::EvidenceOwner,
    CseAttribute::SafetyCaseOwner,
    CseAttribute::ClaimCriteria,
    CseAttribute::EvidenceCriteria,
    CseAttribute::AssessmentStatus,
    CseAttribute::Notes,
};

// The stable wire token ("claim_owner", "assessment_status", ...) used in audit
// payloads and patch operations. Changing one orphans every recorded edit that
// used it.
const char* CseAttributeToken(CseAttribute attribute);
bool ParseCseAttribute(std::string_view token, CseAttribute& out);

// The TaggedValue key the attribute is stored under.
const char* CseAttributeTagKey(CseAttribute attribute);

// The record field the attribute names.
std::string& CseRecordField(CseAssessmentRecord& record, CseAttribute attribute);
const std::string& CseRecordField(const CseAssessmentRecord& record, CseAttribute attribute);

} // namespace core
