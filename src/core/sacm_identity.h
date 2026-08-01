#pragma once

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>

namespace core {

// Generate a random SACM gid (UUID v4 shaped). Not checked for uniqueness --
// use `GenerateUniqueElementGid` when the gid must not collide with an existing
// element in a model.
std::string GenerateSacmGid();

// Generate a random SACM gid that does not collide with any element gid already
// present in `model`.
std::string GenerateUniqueElementGid(const parser::AssuranceCase& model);

// Assign `gid` to the element identified by `element_id`, writing it to BOTH the
// parser model element and (when `package` is non-null) the corresponding SACM
// package element. Returns false and sets `error` -- leaving the model unchanged
// -- if the element is not found in `model` or the package write fails.
//
// Generation is deliberately split out (`GenerateUniqueElementGid`) so an audited
// command can mint the gid ONCE, record it, and force the SAME value on replay.
bool SetElementGid(parser::AssuranceCase& model,
                   sacm::AssuranceCasePackage* package,
                   const std::string& element_id,
                   const std::string& gid,
                   std::string& error);

} // namespace core
