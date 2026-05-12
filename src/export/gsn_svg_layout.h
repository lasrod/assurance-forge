#pragma once

#include "export/gsn_diagram.h"

#include <string>
#include <vector>

namespace export_gsn {

struct GsnSvgLayoutResult {
    std::vector<std::string> warnings;
};

GsnSvgLayoutResult LayoutGsnSvgDiagram(GsnDiagram& diagram);

} // namespace export_gsn
