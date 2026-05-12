#pragma once

#include "export/gsn_svg_exporter.h"

#include <string>

namespace export_gsn {

std::string GenerateGsnSvg(const GsnDiagram& diagram);

} // namespace export_gsn
