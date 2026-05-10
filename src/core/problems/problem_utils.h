#pragma once

#include <string>

namespace core {

class ProblemsManager;

void ClearProblemsByIdPrefix(ProblemsManager& problems_manager, const std::string& prefix);

} // namespace core