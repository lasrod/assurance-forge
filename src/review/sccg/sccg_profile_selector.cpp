#include "review/sccg/sccg_profile_selector.h"

#include "review/sccg/sccg_review.h"

namespace review {

AiReviewGuidelineSelection SelectReviewProfileGuidelines(const core::GuidelineCatalog& guideline_catalog,
                                                         const std::string& review_profile_id) {
    AiReviewGuidelineSelection selection;
    selection.review_profile = guideline_catalog.document.FindReviewProfileById(review_profile_id);
    if (!selection.review_profile) {
        selection.error_message = "SCCG review profile was not found: " + review_profile_id;
        return selection;
    }

    selection.guidelines = guideline_catalog.document.FindGuidelinesByReviewProfile(review_profile_id);
    if (selection.guidelines.empty()) {
        selection.error_message = "SCCG review profile '" + review_profile_id + "' references no valid guidelines.";
    }
    return selection;
}

AiReviewGuidelineSelection SelectReviewProfileForElement(const core::GuidelineCatalog& guideline_catalog,
                                                         const parser::SacmElement& element,
                                                         const core::TreeNode* node) {
    const parser::ReviewProfile* match = nullptr;
    for (const parser::ReviewProfile& profile : guideline_catalog.document.review_profiles) {
        if (!IsReviewProfileCompatibleWithElement(profile, element, node))
            continue;
        if (match != nullptr) {
            AiReviewGuidelineSelection selection;
            selection.error_message = "More than one SCCG review profile applies to the selected element type: '" +
                                      match->display_name + "' and '" + profile.display_name + "'.";
            return selection;
        }
        match = &profile;
    }
    if (match == nullptr) {
        AiReviewGuidelineSelection selection;
        selection.error_message = "No SCCG review profile applies to the selected element type.";
        return selection;
    }
    return SelectReviewProfileGuidelines(guideline_catalog, match->id);
}

} // namespace review
