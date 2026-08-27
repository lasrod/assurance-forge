#include "core/evidence_attributes.h"

#include <system_error>

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

std::string EvidenceLocationForPickedFile(const std::filesystem::path& project_root,
                                          const std::filesystem::path& picked) {
    std::error_code ec;
    const std::filesystem::path absolute_picked = std::filesystem::weakly_canonical(picked, ec);
    const std::filesystem::path chosen = ec ? picked : absolute_picked;
    if (project_root.empty())
        return chosen.generic_string();
    std::error_code root_ec;
    const std::filesystem::path root = std::filesystem::weakly_canonical(project_root, root_ec);
    if (root_ec)
        return chosen.generic_string();
    const std::filesystem::path relative = chosen.lexically_relative(root);
    // Inside the project: a relative path that does not start by climbing out.
    if (relative.empty() || relative.begin() == relative.end() || *relative.begin() == "..")
        return chosen.generic_string();
    return relative.generic_string();
}

} // namespace core
