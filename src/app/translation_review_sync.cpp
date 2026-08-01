#include "app/translation_review_sync.h"

#include "core/element_factory.h"
#include "core/problems/problem_item.h"
#include "core/problems/problem_utils.h"
#include "ui/i18n/localization.h"

#include <vector>

namespace app {
namespace {

constexpr const char* kTranslationReviewProblemPrefix = "translation-review:";

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& element_id) {
    for (const parser::SacmElement& element : model.elements) {
        if (element.id == element_id)
            return &element;
    }
    return nullptr;
}

} // namespace

void SyncTranslationReviewProblems(core::ProblemsManager& problems_manager,
                                   const parser::AssuranceCase* model,
                                   std::unordered_set<std::string>& pending_ids,
                                   bool& out_pending_changed) {
    out_pending_changed = false;
    core::ClearProblemsByIdPrefix(problems_manager, kTranslationReviewProblemPrefix);

    if (!model)
        return;

    std::vector<std::string> stale_ids;
    for (const std::string& element_id : pending_ids) {
        const parser::SacmElement* element = FindElement(*model, element_id);
        if (!element || !core::ElementHasSecondaryTranslation(*element)) {
            stale_ids.push_back(element_id);
            continue;
        }

        core::ProblemItem problem;
        problem.id = kTranslationReviewProblemPrefix + element_id;
        problem.severity = core::ProblemSeverity::Warning;
        problem.source = core::ProblemSource::ModelValidation;
        problem.element_id = element_id;
        problem.type = "TranslationReviewNeeded";
        problem.message = ui::i18n::tr("Text changed — review the secondary-language translation for consistency.");
        problem.quick_fix_label = "Review element";
        problem.quick_fix_payload = element_id;
        problems_manager.AddOrUpdateProblem(problem);
    }

    for (const std::string& stale_id : stale_ids) {
        pending_ids.erase(stale_id);
        out_pending_changed = true;
    }
}

} // namespace app
