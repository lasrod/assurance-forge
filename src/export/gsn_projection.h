#pragma once

#include "export/gsn_svg_exporter.h"
#include "parser/xml_parser.h"

#include <string>
#include <vector>

namespace export_gsn {

struct GsnProjectionResult {
    GsnDiagram diagram;
    std::vector<std::string> warnings;
};

GsnProjectionResult BuildGsnProjection(const parser::AssuranceCase& model);

} // namespace export_gsn
