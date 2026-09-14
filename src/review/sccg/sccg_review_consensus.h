#pragma once

// Agreement across repeated reviews of the same unchanged request.
//
// A safety-case review that reports different findings each time it is run has
// a problem a reviewer cannot see: nothing in a single result says which of its
// findings would survive being asked again. Sampling controls are the usual
// answer, and they are not available -- `gpt-5.5` and `gpt-5.6-sol` both reject
// `temperature`, and `seed` is not a parameter of the Responses API at all, so
// the provider offers no way to make a review repeatable. Measured over 57 runs
// of an unchanged corpus, guideline citations ranged from 3-of-3 to 1-of-3 with
// nothing distinguishing them in the output.
//
// So the variation is measured instead of suppressed: run the same request k
// times, group what comes back by the guideline it cites, and report each
// group with the number of runs that produced it. A finding 3 of 3 runs agree
// on and one that appeared once are then different objects on the reviewer's
// screen, which is the distinction that was missing.
//
// This does NOT make the review deterministic and must not be described as
// doing so. It makes non-determinism visible and lets a caller set a floor
// under what it will show.

#include "core/problems/problem_item.h"
#include "core/reviews/review_proposal.h"
#include "review/sccg/sccg_review.h"

#include <string>
#include <vector>

namespace review {

// One guideline's findings across the runs that cited it.
struct ConsensusFinding {
    std::string guideline_id;
    // How many runs cited this guideline, and out of how many that succeeded.
    // Reported as a pair rather than a ratio because "2 of 3" and "20 of 30"
    // are different evidence and a reviewer is entitled to both numbers.
    int runs_citing = 0;
    int runs_total = 0;

    // The representative finding shown to the reviewer: the one from the
    // earliest run that cited this guideline. Deliberately not a merge of the
    // wordings -- a synthesized message is one no run actually produced, and a
    // reviewer following it back to a record would find nothing that says it.
    core::ProblemItem problem;
    std::string confidence;
    std::vector<core::reviews::PatchOperation> proposedOperations;
    std::string suggestedElementText;

    // Every run's wording for this guideline, in run order, so a reviewer can
    // see whether the runs agreed on the defect or only on the rule.
    std::vector<std::string> messages;

    // Pre-checks that independently flagged this guideline. Not a vote: a
    // deterministic check reached it without a model.
    std::vector<std::string> corroborating_precheck_ids;

    bool unanimous() const {
        return runs_total > 0 && runs_citing == runs_total;
    }
};

struct ConsensusReviewResult {
    int runs_requested = 0;
    int runs_succeeded = 0;
    // Ordered: unanimous findings first, then by descending agreement, then by
    // guideline id. A reviewer reads the best-supported finding first.
    std::vector<ConsensusFinding> findings;
    // Findings that did not meet the caller's floor, kept rather than dropped.
    // A finding one run of three raised is weak evidence, not no evidence, and
    // silently discarding it would hide exactly the instability this measures.
    std::vector<ConsensusFinding> below_threshold;
    // One entry per failed run, worded for a reader. A consensus computed over
    // fewer runs than were asked for must say so: 2 of 2 agreeing when the
    // third run errored is not 3 of 3.
    std::vector<std::string> run_errors;

    bool ok() const {
        return runs_succeeded > 0;
    }
};

// `minimum_runs_citing` is the floor for the main list; 1 keeps everything.
// `precheck_results` supplies corroboration and may be empty.
ConsensusReviewResult BuildConsensusReview(const std::vector<AiReviewParseResult>& run_results,
                                           int runs_requested,
                                           int minimum_runs_citing,
                                           const std::vector<sccg::PrecheckResult>& precheck_results);

} // namespace review
