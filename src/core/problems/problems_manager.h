#pragma once

#include "core/problems/problem_item.h"

#include <optional>
#include <string>
#include <vector>

namespace core {

class ProblemsManager {
public:
    void AddProblem(const ProblemItem& problem);
    void AddOrUpdateProblem(const ProblemItem& problem);
    void RemoveProblem(const std::string& problem_id);
    void ClearProblems();
    void ClearProblemsBySource(ProblemSource source);
    void ClearProblemsForElement(const std::string& element_id);
    void ClearProblemsForElementAndSource(const std::string& element_id, ProblemSource source);

    const std::vector<ProblemItem>& GetProblems() const;
    std::optional<ProblemItem> GetProblemById(const std::string& problem_id) const;

private:
    std::vector<ProblemItem> problems_;
};

}  // namespace core
