#pragma once

// XML parsing entry points for SACM documents.
//
// The data-model PODs (SacmElement, AcpRecord, AssuranceCase) live in
// `core/sacm_model.h` so UI/core layers can depend on them without pulling in
// the parser. Backward-compatible type aliases keep existing `parser::Xxx`
// spellings valid throughout the codebase.

#include "core/sacm_model.h"

#include <expected>
#include <string>

namespace parser {

// SacmElement, AcpRecord, AssuranceCase are declared in core::sacm_model.h
// and exposed under `namespace parser` via transitional aliases there.

// Parses a SACM XML file. Returns the parsed case on success; the error string on failure.
std::expected<AssuranceCase, std::string> parse_sacm_xml(const std::string& file_path);

// Parses SACM XML from a string buffer.
std::expected<AssuranceCase, std::string> parse_sacm_xml_string(const std::string& xml_content);

} // namespace parser
