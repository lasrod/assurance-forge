#pragma once

#include "export/gsn_diagram.h"
#include "parser/xml_parser.h"

#include <string>
#include <vector>

namespace export_gsn {

struct GsnProjectionResult {
    GsnDiagram diagram;
    std::vector<std::string> warnings;
};

// `secondary_language` selects which language the diagram's text is taken
// from: empty exports the primary text, a language code (e.g. "ja") exports
// that language with a per-field fallback to the primary -- the same rule the
// canvas applies, so an exported diagram says what the screen says. Layout is
// unaffected by the choice beyond text metrics.
GsnProjectionResult BuildGsnProjection(const parser::AssuranceCase& model,
                                       const std::string& secondary_language = std::string());

} // namespace export_gsn
