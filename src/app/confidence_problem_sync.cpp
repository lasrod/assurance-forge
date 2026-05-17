#include "app/confidence_problem_sync.h"

#include "core/confidence/confidence_store.h"
#include "core/problems/problem_utils.h"
#include "parser/xml_parser.h"

namespace app {
namespace {

constexpr const char* kConfidenceReviewProblemPrefix = "confidence-review:";

std::string SourceIdOrMain(const std::string& source_id) {
    return source_id.empty() ? "main" : source_id;
}

} // namespace

std::string ConfidenceReviewProblemId(const core::confidence::ConfidenceAssessment& assessment) {
    if (!assessment.id.empty())
        return std::string(kConfidenceReviewProblemPrefix) + assessment.id;
    return std::string(kConfidenceReviewProblemPrefix) + assessment.target.sourceId + ":" + assessment.target.sacmGid;
}

core::ProblemItem MakeProblemFromStaleConfidence(const parser::SacmElement& element,
                                                 const core::confidence::ConfidenceAssessment& assessment) {
    core::ProblemItem problem;
    problem.id = ConfidenceReviewProblemId(assessment);
    problem.severity = core::ProblemSeverity::Warning;
    problem.source = core::ProblemSource::ModelValidation;
    problem.element_id = element.id;
    problem.type = "ConfidenceReviewRequired";
    problem.message = "The stored confidence assessment needs review because this element changed. Review the value "
                      "and reactivate confidence if it is still valid.";
    return problem;
}

void SyncConfidenceProblems(core::ProblemsManager& problems_manager,
                            const parser::AssuranceCase* model,
                            const core::confidence::ConfidenceStore* store,
                            const std::string& source_id) {
    core::ClearProblemsByIdPrefix(problems_manager, kConfidenceReviewProblemPrefix);
    if (!model || !store)
        return;

    const std::string normalized_source_id = SourceIdOrMain(source_id);
    for (const parser::SacmElement& element : model->elements) {
        if (element.gid.empty())
            continue;
        const core::confidence::ConfidenceAssessment* assessment =
            core::confidence::FindAssessment(*store, normalized_source_id, element.gid);
        if (!assessment || !assessment->stale)
            continue;
        problems_manager.AddOrUpdateProblem(MakeProblemFromStaleConfidence(element, *assessment));
    }
}

} // namespace app
