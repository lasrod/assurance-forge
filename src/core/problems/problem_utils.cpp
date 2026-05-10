#include "core/problems/problem_utils.h"

#include "core/problems/problems_manager.h"
#include "core/string_utils.h"

#include <string>
#include <vector>

namespace core {

void ClearProblemsByIdPrefix(ProblemsManager& problems_manager, const std::string& prefix) {
    std::vector<std::string> problem_ids;
    for (const ProblemItem& problem : problems_manager.GetProblems()) {
        if (StartsWith(problem.id, prefix))
            problem_ids.push_back(problem.id);
    }
    for (const std::string& problem_id : problem_ids) {
        problems_manager.RemoveProblem(problem_id);
    }
}

} // namespace core