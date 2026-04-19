#include "gsn_adapter.h"

namespace ui {

static ElementRole MapType(const std::string& t) {
    if (t == "claim") return ElementRole::Claim;
    if (t == "assertedcontext") return ElementRole::Context;
    if (t == "assertedjustification" || t == "justification") return ElementRole::Justification;
    if (t == "assumption") return ElementRole::Assumption;
    if (t == "argumentreasoning") return ElementRole::Strategy;
    if (t == "artifact" || t == "artifactreference" || t == "expression") return ElementRole::Evidence;
    return ElementRole::Other;
}

std::vector<CanvasElement> ConvertFromAssuranceCase(const parser::AssuranceCase& ac) {
    std::vector<CanvasElement> out;
    out.reserve(ac.elements.size());
    for (const auto& e : ac.elements) {
        CanvasElement ce;
        ce.id = e.id.empty() ? e.name : e.id;
        ce.role = MapType(e.type);
        ce.label = !e.name.empty() ? e.name : e.content;
        ce.parent_id = ""; // legacy: no relationships
        out.push_back(ce);
    }
    return out;
}

core::AssuranceTree BuildAssuranceTree(const parser::AssuranceCase& ac) {
    return core::AssuranceTree::Build(ac);
}

} // namespace ui
