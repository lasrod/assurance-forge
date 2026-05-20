#include "core/problems/problems_manager.h"

#include <algorithm>

namespace core {

void ProblemsManager::AddProblem(const ProblemItem& problem) {
    AddOrUpdateProblem(problem);
}

void ProblemsManager::AddOrUpdateProblem(const ProblemItem& problem) {
    if (problem.id.empty())
        return;

    auto it = std::find_if(
        problems_.begin(), problems_.end(), [&](const ProblemItem& existing) { return existing.id == problem.id; });
    if (it == problems_.end()) {
        problems_.push_back(problem);
        return;
    }
    *it = problem;
}

void ProblemsManager::RemoveProblem(const std::string& problem_id) {
    std::erase_if(problems_, [&](const ProblemItem& problem) { return problem.id == problem_id; });
}

void ProblemsManager::ClearProblems() {
    problems_.clear();
}

void ProblemsManager::ClearProblemsBySource(ProblemSource source) {
    std::erase_if(problems_, [&](const ProblemItem& problem) { return problem.source == source; });
}

void ProblemsManager::ClearProblemsForElement(const std::string& element_id) {
    std::erase_if(problems_, [&](const ProblemItem& problem) { return problem.element_id == element_id; });
}

void ProblemsManager::ClearProblemsForElementAndSource(const std::string& element_id, ProblemSource source) {
    std::erase_if(problems_, [&](const ProblemItem& problem) {
        return problem.element_id == element_id && problem.source == source;
    });
}

const std::vector<ProblemItem>& ProblemsManager::GetProblems() const {
    return problems_;
}

std::optional<ProblemItem> ProblemsManager::GetProblemById(const std::string& problem_id) const {
    auto it = std::find_if(
        problems_.begin(), problems_.end(), [&](const ProblemItem& problem) { return problem.id == problem_id; });
    if (it == problems_.end())
        return std::nullopt;
    return *it;
}

} // namespace core
