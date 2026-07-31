#pragma once

#include "core/assurance_tree.h"
#include "core/problems/problem_item.h"
#include "core/project_model.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_proposal.h"
#include "core/sacm_model.h"

#include <cstddef>
#include <vector>

namespace core {

// A presentation-neutral summary of the open assurance project. The Case
// Explorer, project overview, and future reports can all use the same counts
// without making the filesystem layout part of the user-facing information
// architecture.
struct ProjectSummary {
    std::size_t argument_files = 0;
    std::size_t elements = 0;
    std::size_t claims = 0;
    std::size_t strategies = 0;
    std::size_t evidence = 0;
    std::size_t undeveloped = 0;
    std::size_t unlinked_evidence = 0;

    std::size_t open_review_items = 0;
    std::size_t resolved_review_items = 0;
    std::size_t valid_proposals = 0;
    std::size_t broken_proposals = 0;

    std::size_t conformance_files = 0;
    std::size_t exported_reports = 0;
    std::size_t warning_problems = 0;
    std::size_t error_problems = 0;

    std::size_t attention_count() const;
};

ProjectSummary BuildProjectSummary(
    const AssuranceProject* project,
    const AssuranceCase* assurance_case,
    const AssuranceTree* assurance_tree,
    const std::vector<ProblemItem>& problems,
    const std::vector<reviews::ReviewItem>& review_items,
    const std::vector<reviews::ProposalValidityResult>& proposal_validities);

} // namespace core
