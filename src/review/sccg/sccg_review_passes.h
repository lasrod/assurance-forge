#pragma once

// Review passes: one profile reviewed in several requests.
//
// SCCG 0.8.0 lets a profile partition its guidelines by review question --
// `claim_review` into wording, structure, sufficiency and reasoning -- and says
// a tool may send one request per pass and merge the findings under the one
// profile. Profile selection is unchanged; only the request is split.
//
// The reason is measured rather than stylistic. A review mis-attributes a
// finding when two confusable guidelines arrive in the same request: it detects
// the defect and files it under the neighbour. Reviewing claim_review's
// guidelines in four smaller requests instead of one took 31 probes from 18 to
// 26 cited as intended, none worse, and every remaining miss was a confusion
// between two guidelines in the SAME pass.
//
// What the merge must not do is let a pass that failed look like a pass that
// found nothing. A four-pass review with one pass missing is an incomplete
// review, and it is reported as one.

#include "review/sccg/sccg_review.h"

#include <string>
#include <vector>

namespace review {

// One request of a review, and the guidelines it may cite. A profile with no
// passes is reviewed as a single entry whose `pass_id` is empty and whose
// guidelines are the whole profile's.
struct SccgReviewPassRequest {
    std::string pass_id;
    std::string display_name;
    std::string question;
    std::vector<std::string> guideline_ids;
    AiReviewRequestArtifacts request;
};

// What came back for one pass. `error` carries a failure that happened before
// parsing -- the request itself failed -- and is empty otherwise; a parse
// failure is in `result.errorMessage`. Either one makes the pass failed.
struct ReviewPassOutcome {
    std::string pass_id;
    std::string error;
    std::string raw_response;
    AiReviewParseResult result;
};

struct MergedReviewPasses {
    // The findings of every pass that succeeded, in pass order. Its
    // `errorMessage` is set only when NO pass succeeded: a review with some
    // passes done has findings a reviewer should see.
    AiReviewParseResult merged;
    int passes_total = 0;
    std::vector<std::string> failed_pass_ids;
    // One per failed pass, worded for a reader.
    std::vector<std::string> pass_errors;
    // Findings a pass cited under a guideline another pass of the same profile
    // reviews. The pass that owns the guideline was asked about it in its own
    // request, so this is the model answering a question it was not asked --
    // discarded, and said so rather than silently.
    std::vector<std::string> discarded_findings;

    bool complete() const {
        return failed_pass_ids.empty();
    }
    bool any_succeeded() const {
        return static_cast<int>(failed_pass_ids.size()) < passes_total;
    }
};

// `expected_element_id` is checked per pass: a pass that reports reviewing a
// different element has failed, whatever it found.
MergedReviewPasses MergeReviewPasses(const std::vector<ReviewPassOutcome>& outcomes,
                                     const std::vector<SccgReviewPassRequest>& passes,
                                     const std::string& expected_element_id);

// The one editable prompt a debug view shows for a review of several passes,
// each pass under a separator line naming it. `SplitPassPrompts` is the inverse:
// it succeeds only if every separator is still present and in order, and then
// replaces each pass's prompt; otherwise it leaves `passes` untouched. A caller
// that cannot split an edited prompt must not guess which pass an edit belongs
// to.
std::string CombinePassPrompts(const std::vector<SccgReviewPassRequest>& passes);
bool SplitPassPrompts(const std::string& combined, std::vector<SccgReviewPassRequest>& passes);

} // namespace review
