#pragma once

#include "core/sacm_model.h"

namespace sacm {
struct AssuranceCasePackage;
}

namespace app {

void RebuildSacmArgumentPackageFromParser(const parser::AssuranceCase& model, sacm::AssuranceCasePackage& package);

} // namespace app
