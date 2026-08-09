#pragma once

#include "sacm_model.h"

#include <expected>
#include <string>

namespace sacm {

// Parse a SACM XML file into domain model classes.
std::expected<AssuranceCasePackage, std::string> parse_sacm(const std::string& file_path);

// Parse SACM XML from a string buffer.
std::expected<AssuranceCasePackage, std::string> parse_sacm_string(const std::string& xml_content);

} // namespace sacm
