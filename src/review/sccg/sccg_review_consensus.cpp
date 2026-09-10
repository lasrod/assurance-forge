#include "review/sccg/sccg_review_consensus.h"

#include <algorithm>
#include <map>
#include <utility>

namespace review {
namespace {

// Grouping key. The guideline is the unit a reviewer disposes of and the unit
// SCCG names, so two runs objecting to the same element under the same
// guideline are one finding seen twice -- even where they worded it
// differently. Grouping on the message instead would report near-duplicates as
// disagreement and make every review look unstable.
struct FindingKey {
    std::string guideline_id;
    std::string element_id;

    bool operator<(const FindingKey& other) const {
        return std::tie(guideline_id, element_id) < std::tie(other.guideline_id, other.element_id);
    }
    bool operator==(const FindingKey& other) const {
        return guideline_id == other.guideline_id && element_id == other.element_id;
    }
};

bool MoreAgreementFirst(const ConsensusFinding& left, const ConsensusFinding& right) {
    if (left.runs_citing != right.runs_citing)
        return left.runs_citing > right.runs_citing;
    return left.guideline_id < right.guideline_id;
}

} // namespace

ConsensusReviewResult BuildConsensusReview(const std::vector<AiReviewParseResult>& run_results,
                                           int runs_requested,
                                           int minimum_runs_citing,
                                           const std::vector<sccg::PrecheckResult>& precheck_results) {
    ConsensusReviewResult result;
    result.runs_requested = runs_requested;

    std::map<FindingKey, ConsensusFinding> grouped;
    // Insertion order, so the representative finding is the earliest run's and
    // the output does not reorder itself between invocations.
    std::vector<FindingKey> order;

    for (const AiReviewParseResult& run : run_results) {
        if (!run.errorMessage.empty()) {
            result.run_errors.push_back(run.errorMessage);
            continue;
        }
        ++result.runs_succeeded;

        // One vote per guideline per run. A run that raised the same guideline
        // twice against one element has found one thing worth saying twice, and
        // counting it twice would let a single run out-vote the others.
        std::vector<FindingKey> counted_this_run;

        for (std::size_t index = 0; index < run.problems.size(); ++index) {
            const core::ProblemItem& problem = run.problems[index];
            if (problem.guideline_id.empty())
                continue;

            const FindingKey key{problem.guideline_id, problem.element_id};
            const bool already_counted =
                std::find(counted_this_run.begin(), counted_this_run.end(), key) != counted_this_run.end();

            auto found = grouped.find(key);
            if (found == grouped.end()) {
                ConsensusFinding finding;
                finding.guideline_id = problem.guideline_id;
                finding.problem = problem;
                if (index < run.findingConfidences.size())
                    finding.confidence = run.findingConfidences[index];
                if (index < run.proposedOperations.size())
                    finding.proposedOperations = run.proposedOperations[index];
                if (index < run.suggestedElementTexts.size())
                    finding.suggestedElementText = run.suggestedElementTexts[index];
                finding.corroborating_precheck_ids = CorroboratingPrecheckIds(problem.guideline_id, precheck_results);
                found = grouped.emplace(key, std::move(finding)).first;
                order.push_back(key);
            }

            found->second.messages.push_back(problem.message);
            if (!already_counted) {
                ++found->second.runs_citing;
                counted_this_run.push_back(key);
            }
        }
    }

    std::vector<ConsensusFinding> findings;
    findings.reserve(order.size());
    for (const FindingKey& key : order) {
        ConsensusFinding finding = grouped[key];
        finding.runs_total = result.runs_succeeded;
        findings.push_back(std::move(finding));
    }

    std::stable_sort(findings.begin(), findings.end(), MoreAgreementFirst);

    const int floor_value = minimum_runs_citing < 1 ? 1 : minimum_runs_citing;
    for (ConsensusFinding& finding : findings) {
        if (finding.runs_citing >= floor_value)
            result.findings.push_back(std::move(finding));
        else
            result.below_threshold.push_back(std::move(finding));
    }
    return result;
}

} // namespace review
