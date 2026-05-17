#pragma once

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>

namespace core {

enum class EnsureGidResult {
    AlreadyPresent,
    Generated,
    Failed,
};

std::string GenerateSacmGid();
EnsureGidResult EnsureElementGid(parser::AssuranceCase& model,
                                 sacm::AssuranceCasePackage* package,
                                 parser::SacmElement& element,
                                 std::string& error);

} // namespace core