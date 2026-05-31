#pragma once

#include "core/problems/problems_manager.h"
#include "core/sacm_model.h"

#include <string>
#include <unordered_set>

namespace app {

// Rebuilds the "translation review needed" problems from the set of element IDs
// the user flagged by editing a translated element. For each pending element
// that still exists and still carries a secondary-language translation, emits a
// Warning problem (type "TranslationReviewNeeded"). Pending IDs whose element
// was deleted or lost its translation are dropped from `pending_ids` and
// `out_pending_changed` is set so the caller can persist the pruned set.
void SyncTranslationReviewProblems(core::ProblemsManager& problems_manager,
                                   const parser::AssuranceCase* model,
                                   std::unordered_set<std::string>& pending_ids,
                                   bool& out_pending_changed);

} // namespace app
