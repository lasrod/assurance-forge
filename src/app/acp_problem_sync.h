#pragma once

#include "core/problems/problems_manager.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace app {

void SyncAcpProblems(core::ProblemsManager& problems_manager,
                     const parser::AssuranceCase* model,
                     const sacm::AssuranceCasePackage* package);

} // namespace app