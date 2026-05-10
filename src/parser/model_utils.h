#pragma once

#include <string>

namespace parser {

struct AssuranceCase;
struct SacmElement;

const SacmElement* FindElementById(const AssuranceCase& model, const std::string& id);
SacmElement* FindElementById(AssuranceCase& model, const std::string& id);
const SacmElement* FindElementByIdOrGidValue(const AssuranceCase& model, const std::string& id_or_gid);
SacmElement* FindElementByIdOrGid(AssuranceCase& model, const std::string& id, const std::string& gid);
const SacmElement* FindElementByIdOrGid(const AssuranceCase& model, const std::string& id, const std::string& gid);

} // namespace parser