#include "core/evidence_attributes.h"

namespace core {

const char* EvidenceAttributeToken(EvidenceAttribute attribute) {
    switch (attribute) {
    case EvidenceAttribute::Owner:
        return "owner";
    case EvidenceAttribute::Type:
        return "type";
    case EvidenceAttribute::Version:
        return "version";
    case EvidenceAttribute::Date:
        return "date";
    case EvidenceAttribute::Maturity:
        return "maturity";
    case EvidenceAttribute::ControlledEnvironment:
        return "controlled_environment";
    case EvidenceAttribute::Notes:
        return "notes";
    }
    return "";
}

bool ParseEvidenceAttribute(std::string_view token, EvidenceAttribute& out) {
    for (const EvidenceAttribute attribute : kAllEvidenceAttributes) {
        if (token == EvidenceAttributeToken(attribute)) {
            out = attribute;
            return true;
        }
    }
    return false;
}

std::string& EvidenceRecordField(EvidenceRecord& record, EvidenceAttribute attribute) {
    switch (attribute) {
    case EvidenceAttribute::Owner:
        return record.owner;
    case EvidenceAttribute::Type:
        return record.type;
    case EvidenceAttribute::Version:
        return record.version;
    case EvidenceAttribute::Date:
        return record.date;
    case EvidenceAttribute::Maturity:
        return record.maturity;
    case EvidenceAttribute::ControlledEnvironment:
        return record.controlled_environment;
    case EvidenceAttribute::Notes:
        return record.notes;
    }
    return record.notes;
}

const std::string& EvidenceRecordField(const EvidenceRecord& record, EvidenceAttribute attribute) {
    return EvidenceRecordField(const_cast<EvidenceRecord&>(record), attribute);
}

} // namespace core
