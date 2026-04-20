#pragma once

#include "sacm_model.h"
#include <string>

namespace sacm {

struct SacmParseResult {
    bool success = false;
    std::string error_message;
    AssuranceCasePackage package;
};

// Parse a SACM XML file into domain model classes.
SacmParseResult parse_sacm(const std::string& file_path);

// Parse SACM XML from a string buffer.
SacmParseResult parse_sacm_string(const std::string& xml_content);

}  // namespace sacm
