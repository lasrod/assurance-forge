#pragma once

#include "core/problems/problems_manager.h"
#include "parser/xml_parser.h"

namespace app {

// Reports structural defects in the argument itself, as distinct from the ACP,
// terminology, review and confidence syncs which report on things attached to
// it: circular support (`core::FindSupportCycles`) and the GSN v3 Core and
// Dialectic well-formedness rules (`core::CheckGsnWellFormedness`).
//
// Every problem it raises carries the id of the GSN v3 requirement it enforces
// in `guideline_id`, so a diagnostic can be traced back to the standard through
// docs/gsn/gsn-v3-conformance-matrix.md.
void SyncStructureProblems(core::ProblemsManager& problems_manager, const parser::AssuranceCase* model);

} // namespace app
