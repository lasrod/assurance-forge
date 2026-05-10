#pragma once

#include "core/problems/problems_manager.h"

#include <functional>
#include <string>

namespace parser {
struct AssuranceCase;
}

namespace sacm {
struct AssuranceCasePackage;
}

namespace app {

using TerminologySuggestionIgnoredFn = std::function<bool(const std::string&, const std::string&)>;

void SyncTerminologyProblems(core::ProblemsManager& problems_manager,
                             const parser::AssuranceCase* model,
                             const sacm::AssuranceCasePackage* package,
                             const TerminologySuggestionIgnoredFn& is_suggestion_ignored);

} // namespace app
