#pragma once

#include "export/gsn_svg_exporter.h"

#include <string>
#include <vector>

namespace export_gsn {

struct GsnLayoutResult {
    std::vector<std::string> warnings;
};

GsnLayoutResult LayoutGsnDiagram(GsnDiagram& diagram);

} // namespace export_gsn
