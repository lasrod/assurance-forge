#include "review/sccg/sccg_review_preparation.h"

#include "core/guideline_catalog.h"
#include "review/sccg/sccg_profile_selector.h"

#include <optional>
#include <utility>

namespace review {
namespace {

SccgReviewPreparation Fail(SccgReviewPreparationFailure failure, std::string message) {
    SccgReviewPreparation preparation;
    preparation.failure = failure;
    preparation.error_message = std::move(message);
    return preparation;
}

std::vector<std::string> GuidelineIds(const std::vector<const parser::Guideline*>& guidelines) {
    std::vector<std::string> ids;
    ids.reserve(guidelines.size());
    for (const parser::Guideline* guideline : guidelines) {
        if (guideline && !guideline->id.empty())
            ids.push_back(guideline->id);
    }
    return ids;
}

} // namespace

SccgReviewPreparation PrepareSccgReview(const parser::AssuranceCase* assurance_case,
                                        const core::AssuranceTree& tree,
                                        const std::string& selected_element_id,
                                        const std::string& review_profile_id,
                                        const AiReviewCaseContext& case_context,
                                        const core::GuidelineCatalog* catalog) {
    if (selected_element_id.empty())
        return Fail(SccgReviewPreparationFailure::NoSelection, "No GSN element is selected for AI review.");
    if (!assurance_case)
        return Fail(SccgReviewPreparationFailure::NoCase, "No assurance case is loaded for AI review.");

    const parser::SacmElement* selected_element = FindSacmElement(*assurance_case, selected_element_id);
    if (!selected_element)
        return Fail(SccgReviewPreparationFailure::ElementNotFound, "Selected element was not found.");

    SccgReviewPreparation preparation;
    preparation.element_id = selected_element_id;

    const core::TreeNode* selected_node = core::FindTreeNode(tree, selected_element_id);
    preparation.element_type = AiReviewElementType(*selected_element, selected_node);
    preparation.element_role = SccgElementRoleForElement(*selected_element, selected_node);

    if (!IsSupportedAiReviewElement(*selected_element)) {
        preparation.failure = SccgReviewPreparationFailure::UnsupportedElementType;
        preparation.error_message = "AI Review does not support the selected element type.";
        return preparation;
    }

    std::string payload_error;
    if (!BuildAiReviewPayload(*assurance_case, tree, selected_element_id, preparation.payload, payload_error)) {
        preparation.failure = SccgReviewPreparationFailure::PayloadFailed;
        preparation.error_message = payload_error.empty() ? "AI review payload could not be created." : payload_error;
        return preparation;
    }
    preparation.element_type = preparation.payload.selected.type;

    // Loaded here only when the caller holds none. A harness reviewing many
    // elements loads the SCCG distribution once; the application keeps its
    // per-review load, where re-reading the catalog is what picks up a
    // distribution the user replaced between reviews.
    std::optional<core::GuidelineCatalog> loaded_catalog;
    if (!catalog) {
        loaded_catalog.emplace();
        std::string guideline_error;
        if (!core::LoadGuidelineCatalog(*loaded_catalog, guideline_error)) {
            preparation.failure = SccgReviewPreparationFailure::CatalogUnavailable;
            preparation.error_message = "SCCG guidelines could not be loaded for AI review: " + guideline_error;
            return preparation;
        }
        catalog = &*loaded_catalog;
    }

    const AiReviewGuidelineSelection guideline_selection =
        review_profile_id.empty() ? SelectReviewProfileForElement(*catalog, *selected_element, selected_node)
                                  : SelectReviewProfileGuidelines(*catalog, review_profile_id);

    if (guideline_selection.review_profile) {
        preparation.review_profile_id = guideline_selection.review_profile->id;
        preparation.review_profile_name = guideline_selection.review_profile->display_name;
    } else {
        preparation.review_profile_id = review_profile_id;
    }

    if (!guideline_selection.error_message.empty()) {
        preparation.failure = SccgReviewPreparationFailure::ProfileSelectionFailed;
        preparation.error_message = guideline_selection.error_message;
        return preparation;
    }

    if (guideline_selection.review_profile &&
        !IsReviewProfileCompatibleWithElement(
            catalog->document, *guideline_selection.review_profile, *selected_element, selected_node)) {
        preparation.failure = SccgReviewPreparationFailure::ProfileIncompatible;
        preparation.error_message = "SCCG review profile '" + guideline_selection.review_profile->display_name +
                                    "' does not apply to the selected element type.";
        return preparation;
    }

    std::string data_package_error;
    if (!CollectAiReviewDataPackages(*assurance_case,
                                     tree,
                                     selected_element_id,
                                     catalog->document,
                                     guideline_selection.review_profile,
                                     preparation.data_packages,
                                     data_package_error,
                                     &case_context)) {
        preparation.failure = SccgReviewPreparationFailure::DataPackagesFailed;
        preparation.error_message =
            data_package_error.empty() ? "AI review data packages could not be collected." : data_package_error;
        return preparation;
    }

    preparation.precheck_results = sccg::RunPrechecks(catalog->document, *assurance_case, tree, selected_element_id);
    preparation.request = BuildAiReviewRequestArtifacts(preparation.payload,
                                                        guideline_selection.guidelines,
                                                        guideline_selection.review_profile,
                                                        &preparation.data_packages,
                                                        &preparation.precheck_results);
    preparation.guideline_ids = GuidelineIds(guideline_selection.guidelines);
    preparation.reviewed_element_ids = ReviewedElementIds(preparation.payload, preparation.data_packages);
    return preparation;
}

} // namespace review
