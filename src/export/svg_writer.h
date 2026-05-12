#pragma once

#include "export/gsn_diagram.h"

#include <string>

namespace export_gsn {

std::string GenerateGsnSvg(const GsnDiagram& diagram);

} // namespace export_gsn
