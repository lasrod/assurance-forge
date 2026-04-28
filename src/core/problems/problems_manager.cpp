#include "core/problems/problems_manager.h"

#include <algorithm>

namespace core {

const char* ToString(ProblemSeverity severity) {
    switch (severity) {
        case ProblemSeverity::Info: return "Info";
        case ProblemSeverity::Warning: return "Warning";
        case ProblemSeverity::Error: return "Error";
    }
    return "Unknown";
}

const char* ToString(ProblemSource source) {
    switch (source) {
        case ProblemSource::Manual: return "Manual";
        case ProblemSource::ModelValidation: return "ModelValidation";
        case ProblemSource::GuidelineReview: return "GuidelineReview";
        case ProblemSource::AIReview: return "AIReview";
        case ProblemSource::ImportExport: return "ImportExport";
    }
    return "Unknown";
}

void ProblemsManager::AddProblem(const ProblemItem& problem) {
    AddOrUpdateProblem(problem);
}

void ProblemsManager::AddOrUpdateProblem(const ProblemItem& problem) {
    if (problem.id.empty()) return;

    auto it = std::find_if(problems_.begin(), problems_.end(), [&](const ProblemItem& existing) {
        return existing.id == problem.id;
    });
    if (it == problems_.end()) {
        problems_.push_back(problem);
        return;
    }
    *it = problem;
}

void ProblemsManager::RemoveProblem(const std::string& problem_id) {
    problems_.erase(std::remove_if(problems_.begin(), problems_.end(), [&](const ProblemItem& problem) {
        return problem.id == problem_id;
    }), problems_.end());
}

void ProblemsManager::ClearProblems() {
    problems_.clear();
}

void ProblemsManager::ClearProblemsBySource(ProblemSource source) {
    problems_.erase(std::remove_if(problems_.begin(), problems_.end(), [&](const ProblemItem& problem) {
        return problem.source == source;
    }), problems_.end());
}

void ProblemsManager::ClearProblemsForElement(const std::string& element_id) {
    problems_.erase(std::remove_if(problems_.begin(), problems_.end(), [&](const ProblemItem& problem) {
        return problem.element_id == element_id;
    }), problems_.end());
}

const std::vector<ProblemItem>& ProblemsManager::GetProblems() const {
    return problems_;
}

std::optional<ProblemItem> ProblemsManager::GetProblemById(const std::string& problem_id) const {
    auto it = std::find_if(problems_.begin(), problems_.end(), [&](const ProblemItem& problem) {
        return problem.id == problem_id;
    });
    if (it == problems_.end()) return std::nullopt;
    return *it;
}

}  // namespace core
