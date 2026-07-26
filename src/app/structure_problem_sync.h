#pragma once

#include "core/problems/problems_manager.h"
#include "parser/xml_parser.h"

namespace app {

// Reports structural defects in the argument itself, as distinct from the ACP,
// terminology, review and confidence syncs which report on things attached to
// it. Currently: circular support (see core::FindSupportCycles).
void SyncStructureProblems(core::ProblemsManager& problems_manager, const parser::AssuranceCase* model);

} // namespace app
