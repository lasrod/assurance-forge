#pragma once

#include "core/problems/problem_item.h"
#include "core/problems/problems_manager.h"
#include "core/sacm_model.h"

#include <string>

namespace core::confidence {
struct ConfidenceAssessment;
struct ConfidenceStore;
} // namespace core::confidence

namespace app {

std::string ConfidenceReviewProblemId(const core::confidence::ConfidenceAssessment& assessment);
core::ProblemItem MakeProblemFromStaleConfidence(const parser::SacmElement& element,
                                                 const core::confidence::ConfidenceAssessment& assessment);
void SyncConfidenceProblems(core::ProblemsManager& problems_manager,
                            const parser::AssuranceCase* model,
                            const core::confidence::ConfidenceStore* store,
                            const std::string& source_id);

} // namespace app
