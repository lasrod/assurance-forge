#pragma once

// Compares the legacy parser's output against the library-backed projection.
//
// This is the point of Stage 3. The two will not agree at first, and the
// disagreements are the deliverable: each one is either a projection bug to fix
// or a legacy-parser behaviour to consciously drop. Depending on the library
// before this list is empty would silently change what users see.
//
// The difference shape deliberately mirrors sacm::compare::semantic_compare's
// {category, path, message} so reports read the same whichever comparison
// produced them.

#include "core/sacm_model.h"

#include <string>
#include <vector>

namespace sacm_adapter {

struct ProjectionDifference {
    std::string category; // "element-missing", "element-extra", "field", "case"
    std::string path;     // element id, or the case-level field
    std::string message;

    friend bool operator==(const ProjectionDifference&, const ProjectionDifference&) = default;
};

// `legacy` is what the application renders today; `projected` is what it would
// render from the library. Order-insensitive: elements are paired by id.
std::vector<ProjectionDifference> diff_cases(const core::AssuranceCase& legacy, const core::AssuranceCase& projected);

} // namespace sacm_adapter
