#pragma once

#include "sacm_model.h"
#include <string>

namespace sacm {

// Serialize an AssuranceCasePackage to SACM XML string.
std::string serialize_sacm(const AssuranceCasePackage& package);

// Serialize an AssuranceCasePackage to a SACM XML file.
bool serialize_sacm_to_file(const AssuranceCasePackage& package, const std::string& file_path);

}  // namespace sacm
