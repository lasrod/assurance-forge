#pragma once

#include "core/sacm_model.h"

namespace sacm {
struct AssuranceCasePackage;
}

namespace core {

// Rebuild the first argument package of `package` from the parser-side
// `model`. Preserves visible-terminology artifact references and asserted
// contexts that target terms still present in the package. Lives in `core`
// (not `app`) so audited commands that mutate the parser model can keep
// the SACM mirror in sync without crossing the layer boundary into `app`.
void RebuildSacmArgumentPackageFromParser(const parser::AssuranceCase& model, sacm::AssuranceCasePackage& package);

} // namespace core
