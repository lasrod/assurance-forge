#pragma once

// The evidence register's SACM-backed columns, named once. The register, the
// audited command, the draft operation and the MCP wire all address a column
// by this token, so a column can be added in one place and every path that
// writes it stays in step.

#include "core/sacm_model.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace core {

enum class EvidenceAttribute {
    Owner,
    Type,
    Version,
    Date,
    Maturity,
    ControlledEnvironment,
    Notes,
};

inline constexpr std::array<EvidenceAttribute, 7> kAllEvidenceAttributes = {
    EvidenceAttribute::Owner,
    EvidenceAttribute::Type,
    EvidenceAttribute::Version,
    EvidenceAttribute::Date,
    EvidenceAttribute::Maturity,
    EvidenceAttribute::ControlledEnvironment,
    EvidenceAttribute::Notes,
};

// The stable wire token ("owner", "version", ...), used in audit payloads and
// patch operations. Changing one orphans every recorded edit that used it.
const char* EvidenceAttributeToken(EvidenceAttribute attribute);
bool ParseEvidenceAttribute(std::string_view token, EvidenceAttribute& out);

// The record field the attribute names.
std::string& EvidenceRecordField(EvidenceRecord& record, EvidenceAttribute attribute);
const std::string& EvidenceRecordField(const EvidenceRecord& record, EvidenceAttribute attribute);

// The location to record for a file the user picked: relative to
// `project_root` (generic separators) when the file is inside the project, so
// the project stays movable; the absolute path otherwise. An empty root
// records the absolute path.
std::string EvidenceLocationForPickedFile(const std::filesystem::path& project_root,
                                          const std::filesystem::path& picked);

} // namespace core
