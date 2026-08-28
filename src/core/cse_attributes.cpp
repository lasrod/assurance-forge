#include "core/cse_attributes.h"

namespace core {

const char* CseAttributeToken(CseAttribute attribute) {
    switch (attribute) {
    case CseAttribute::ClaimOwner:
        return "claim_owner";
    case CseAttribute::EvidenceOwner:
        return "evidence_owner";
    case CseAttribute::SafetyCaseOwner:
        return "safety_case_owner";
    case CseAttribute::ClaimCriteria:
        return "claim_criteria";
    case CseAttribute::EvidenceCriteria:
        return "evidence_criteria";
    case CseAttribute::AssessmentStatus:
        return "assessment_status";
    case CseAttribute::Notes:
        return "notes";
    }
    return "";
}

bool ParseCseAttribute(std::string_view token, CseAttribute& out) {
    for (const CseAttribute attribute : kAllCseAttributes) {
        if (token == CseAttributeToken(attribute)) {
            out = attribute;
            return true;
        }
    }
    return false;
}

const char* CseAttributeTagKey(CseAttribute attribute) {
    switch (attribute) {
    case CseAttribute::ClaimOwner:
        return kCseClaimOwnerTagKey;
    case CseAttribute::EvidenceOwner:
        return kCseEvidenceOwnerTagKey;
    case CseAttribute::SafetyCaseOwner:
        return kCseSafetyCaseOwnerTagKey;
    case CseAttribute::ClaimCriteria:
        return kCseClaimCriteriaTagKey;
    case CseAttribute::EvidenceCriteria:
        return kCseEvidenceCriteriaTagKey;
    case CseAttribute::AssessmentStatus:
        return kCseAssessmentStatusTagKey;
    case CseAttribute::Notes:
        return kCseNotesTagKey;
    }
    return "";
}

std::string& CseRecordField(CseAssessmentRecord& record, CseAttribute attribute) {
    switch (attribute) {
    case CseAttribute::ClaimOwner:
        return record.claim_owner;
    case CseAttribute::EvidenceOwner:
        return record.evidence_owner;
    case CseAttribute::SafetyCaseOwner:
        return record.safety_case_owner;
    case CseAttribute::ClaimCriteria:
        return record.claim_criteria;
    case CseAttribute::EvidenceCriteria:
        return record.evidence_criteria;
    case CseAttribute::AssessmentStatus:
        return record.assessment_status;
    case CseAttribute::Notes:
        return record.notes;
    }
    return record.notes;
}

const std::string& CseRecordField(const CseAssessmentRecord& record, CseAttribute attribute) {
    switch (attribute) {
    case CseAttribute::ClaimOwner:
        return record.claim_owner;
    case CseAttribute::EvidenceOwner:
        return record.evidence_owner;
    case CseAttribute::SafetyCaseOwner:
        return record.safety_case_owner;
    case CseAttribute::ClaimCriteria:
        return record.claim_criteria;
    case CseAttribute::EvidenceCriteria:
        return record.evidence_criteria;
    case CseAttribute::AssessmentStatus:
        return record.assessment_status;
    case CseAttribute::Notes:
        return record.notes;
    }
    return record.notes;
}

} // namespace core
