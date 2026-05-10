#include "parser/model_utils.h"

#include "parser/xml_parser.h"

#include <algorithm>

namespace parser {

const SacmElement* FindElementById(const AssuranceCase& model, const std::string& id) {
    auto found = std::find_if(
        model.elements.begin(), model.elements.end(), [&](const SacmElement& element) { return element.id == id; });
    return found == model.elements.end() ? nullptr : &*found;
}

SacmElement* FindElementById(AssuranceCase& model, const std::string& id) {
    auto found = std::find_if(
        model.elements.begin(), model.elements.end(), [&](const SacmElement& element) { return element.id == id; });
    return found == model.elements.end() ? nullptr : &*found;
}

const SacmElement* FindElementByIdOrGidValue(const AssuranceCase& model, const std::string& id_or_gid) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const SacmElement& element) {
        return element.id == id_or_gid || element.gid == id_or_gid;
    });
    return found == model.elements.end() ? nullptr : &*found;
}

SacmElement* FindElementByIdOrGid(AssuranceCase& model, const std::string& id, const std::string& gid) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const SacmElement& element) {
        return (!id.empty() && element.id == id) || (!gid.empty() && element.gid == gid);
    });
    return found == model.elements.end() ? nullptr : &*found;
}

const SacmElement* FindElementByIdOrGid(const AssuranceCase& model, const std::string& id, const std::string& gid) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const SacmElement& element) {
        return (!id.empty() && element.id == id) || (!gid.empty() && element.gid == gid);
    });
    return found == model.elements.end() ? nullptr : &*found;
}

bool IsRelationshipElement(const SacmElement& element) {
    return element.type == "assertedinference" || element.type == "assertedcontext" ||
           element.type == "assertedevidence";
}

std::string ElementTerminologyText(const SacmElement& element) {
    if (element.type == "claim" || element.type == "argumentreasoning")
        return element.content;
    return element.description;
}

} // namespace parser