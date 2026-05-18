#pragma once

#include "core/sacm_model.h"

#include <string>

namespace parser {

const SacmElement* FindElementById(const AssuranceCase& model, const std::string& id);
SacmElement* FindElementById(AssuranceCase& model, const std::string& id);
const SacmElement* FindElementByIdOrGidValue(const AssuranceCase& model, const std::string& id_or_gid);
SacmElement* FindElementByIdOrGid(AssuranceCase& model, const std::string& id, const std::string& gid);
const SacmElement* FindElementByIdOrGid(const AssuranceCase& model, const std::string& id, const std::string& gid);
bool IsRelationshipElement(const SacmElement& element);
std::string ElementTerminologyText(const SacmElement& element);

} // namespace parser