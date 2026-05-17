#pragma once

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>

namespace core {

std::string GenerateSacmGid();
bool EnsureElementGid(parser::AssuranceCase& model,
                      sacm::AssuranceCasePackage* package,
                      parser::SacmElement& element,
                      std::string& error);

} // namespace core