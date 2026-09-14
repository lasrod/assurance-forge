#pragma once

// Steps 1-5 of the SCCG-guided review workflow, in one place.
//
// The workflow SCCG publishes runs: resolve the selected element, load the
// catalog, select the one review profile for the element's role, gather that
// profile's data packages and record the availability state of every package
// that could not be supplied, run the deterministic pre-checks, and assemble
// the request. Step 6 -- calling a provider -- is not here and must not be:
// `review` owns the method, `ai` owns inference.
//
// This lived inside `AiReviewController::BeginReviewForSelection`, which meant
// the only way to run an SCCG review was to render a frame. Extracting it makes
// the method callable from anywhere that has a case and a tree, so the
// application and an offline evaluation harness assemble the same request from
// the same code rather than from two implementations that agree until one is
// edited.
//
// What stays in the controller is what only the application can do: reporting a
// failure as a problem or a review item, emitting canvas events, and recording
// the outcome against the reviewed element.

#include "core/assurance_tree.h"
#include "parser/xml_parser.h"
#include "review/sccg/sccg_prechecks.h"
#include "review/sccg/sccg_review.h"
#include "review/sccg/sccg_review_passes.h"

#include <string>
#include <vector>

namespace core {
struct GuidelineCatalog;
} // namespace core

namespace review {

// Why the request was not assembled. Distinct values rather than an error
// string because the caller's response differs per case: a review of nothing
// selected is a note to the user, a catalog that would not load is a failure
// recorded against the element.
enum class SccgReviewPreparationFailure {
    None,
    // No element was selected.
    NoSelection,
    // No assurance case is loaded.
    NoCase,
    // The selected id is not in the case.
    ElementNotFound,
    // The element is of a type SCCG review does not cover.
    UnsupportedElementType,
    // The selected/parent/children payload could not be built.
    PayloadFailed,
    // The SCCG catalog could not be loaded.
    CatalogUnavailable,
    // No profile for the element's role, or more than one. Selection fails
    // closed: SCCG names exactly one profile per role, so neither is reviewable.
    ProfileSelectionFailed,
    // A profile was named explicitly and does not apply to this element's role.
    ProfileIncompatible,
    // A required data package could not be collected.
    DataPackagesFailed,
};

// Everything the request was built from, kept beside the request itself. A
// review record has to state which profile ran, which guidelines it carried,
// which packages were supplied and which were declared absent, and what the
// pre-checks decided; assembling that from the pieces afterwards would mean
// re-deriving facts this function already established.
struct SccgReviewPreparation {
    SccgReviewPreparationFailure failure = SccgReviewPreparationFailure::None;
    // English. The caller owns how (and whether) it is shown.
    std::string error_message;

    AiReviewRequestArtifacts request;
    AiReviewPayload payload;
    // Empty when the failure happened before the element was resolved.
    std::string element_id;
    std::string element_type;
    // The SCCG element role the profile was selected on: claim, strategy,
    // evidence, context, assumption, justification, challenge.
    std::string element_role;

    std::string review_profile_id;
    std::string review_profile_name;
    std::vector<std::string> guideline_ids;
    std::vector<std::string> reviewed_element_ids;

    AiReviewDataPackageBundle data_packages;
    std::vector<sccg::PrecheckResult> precheck_results;

    // The requests to send. One per review pass when the profile publishes
    // passes (SCCG 0.8.0's claim_review has four), otherwise one covering the
    // whole profile with an empty `pass_id`. Callers send these, not `request`:
    // `request` is the whole profile in one request, kept for a caller that
    // deliberately wants the unsplit review -- the evaluation harness compares
    // the two -- and for display.
    std::vector<SccgReviewPassRequest> passes;

    bool ok() const {
        return failure == SccgReviewPreparationFailure::None;
    }
};

// `review_profile_id` empty selects the profile from the element's role, which
// is the application's own path -- one AI Review action, no menu of intents.
// Naming one is for a caller deliberately exercising a profile, and is checked
// against the element's role rather than trusted.
//
// `catalog` may be supplied by a caller that already holds one, which is the
// difference between loading the SCCG distribution once and loading it per
// element. Null loads one for this call, as the application does.
SccgReviewPreparation PrepareSccgReview(const parser::AssuranceCase* assurance_case,
                                        const core::AssuranceTree& tree,
                                        const std::string& selected_element_id,
                                        const std::string& review_profile_id,
                                        const AiReviewCaseContext& case_context,
                                        const core::GuidelineCatalog* catalog = nullptr);

} // namespace review
