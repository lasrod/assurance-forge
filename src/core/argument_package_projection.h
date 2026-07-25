#pragma once

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>
#include <string_view>

// Argument-package projection. Given the full project model, build a filtered
// `parser::AssuranceCase` containing only the elements (and ACPs) belonging to
// a single `sacm::ArgumentPackage`. Used by the GSN canvas's per-package tabs
// and by the History Timeline so reconstructed historical views can match the
// canvas scope.
//
// This is pure logic with no UI dependency; it lives in `core` so audit
// reconstruction in `core::audit::history_reconstruction` can call it without
// crossing layer boundaries.
namespace core {

// Locate an `ArgumentPackage` inside `package` by id or gid. Either argument
// may be empty; matching prefers a non-empty `package_id` and falls back to
// `package_gid`. Returns nullptr when no match is found.
const sacm::ArgumentPackage* FindArgumentPackageByIdentity(const sacm::AssuranceCasePackage& package,
                                                           std::string_view package_id,
                                                           std::string_view package_gid);

// Project `source_model` down to the elements and ACPs owned by
// `argument_package`. The returned case copies the package's id/name/
// description (falling back to the source's id and `fallback_name` for empty
// fields) and contains only those elements whose `id` or `gid` is owned by
// the package -- plus the render-only bare-strategy placements
// (`core::SynthesizeBareStrategyPlacements`), which are deliberately NOT in the
// package and would otherwise be filtered away, leaving a strategy that has no
// sub-goal yet floating unattached from its goal.
parser::AssuranceCase BuildArgumentPackageProjection(const parser::AssuranceCase& source_model,
                                                     const sacm::ArgumentPackage& argument_package,
                                                     std::string_view fallback_name);

} // namespace core
