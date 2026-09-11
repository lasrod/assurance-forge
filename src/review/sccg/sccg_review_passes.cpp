#include "review/sccg/sccg_review_passes.h"

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <utility>

namespace review {
namespace {

std::string PassLabel(const SccgReviewPassRequest& pass) {
    return pass.pass_id.empty() ? std::string("The review") : "Pass '" + pass.pass_id + "'";
}

std::string Separator(std::size_t index, std::size_t count, const std::string& pass_id) {
    return "===== SCCG review pass " + std::to_string(index + 1) + "/" + std::to_string(count) + ": " + pass_id +
           " =====";
}

const ReviewPassOutcome* FindOutcome(const std::vector<ReviewPassOutcome>& outcomes, const std::string& pass_id) {
    const auto found = std::find_if(
        outcomes.begin(), outcomes.end(), [&](const ReviewPassOutcome& outcome) { return outcome.pass_id == pass_id; });
    return found == outcomes.end() ? nullptr : &*found;
}

void Fail(MergedReviewPasses& merged, const SccgReviewPassRequest& pass, std::string reason) {
    merged.failed_pass_ids.push_back(pass.pass_id);
    merged.pass_errors.push_back(std::move(reason));
}

} // namespace

MergedReviewPasses MergeReviewPasses(const std::vector<ReviewPassOutcome>& outcomes,
                                     const std::vector<SccgReviewPassRequest>& passes,
                                     const std::string& expected_element_id) {
    MergedReviewPasses merged;
    merged.passes_total = static_cast<int>(passes.size());

    // Which pass owns each guideline. SCCG guarantees a partition and the
    // loader refuses a catalogue that breaks it, so an id maps to one pass.
    std::map<std::string, std::string> owning_pass;
    for (const SccgReviewPassRequest& pass : passes) {
        for (const std::string& guideline_id : pass.guideline_ids)
            owning_pass[guideline_id] = pass.pass_id;
    }

    // An unknown reference -- an id no pass owns -- stays a finding, flagged, as
    // it does in a single-request review. Raised again by a second pass it is
    // the same reference, and one flag is the honest count.
    std::map<std::string, std::string> unknown_reference_first_pass;

    for (const SccgReviewPassRequest& pass : passes) {
        const std::string label = PassLabel(pass);
        const ReviewPassOutcome* outcome = FindOutcome(outcomes, pass.pass_id);
        if (outcome == nullptr) {
            Fail(merged, pass, label + " returned nothing.");
            continue;
        }
        if (!outcome->error.empty()) {
            Fail(merged, pass, label + " failed: " + outcome->error);
            continue;
        }
        const AiReviewParseResult& result = outcome->result;
        if (!result.errorMessage.empty()) {
            Fail(merged, pass, label + " returned a response that could not be parsed: " + result.errorMessage);
            continue;
        }
        if (!result.reviewedElementId.empty() && result.reviewedElementId != expected_element_id) {
            Fail(merged,
                 pass,
                 std::format("{} reported reviewing '{}' rather than '{}'.",
                             label,
                             result.reviewedElementId,
                             expected_element_id));
            continue;
        }

        if (merged.merged.reviewedElementId.empty())
            merged.merged.reviewedElementId = result.reviewedElementId;
        if (merged.merged.reviewedElementType.empty())
            merged.merged.reviewedElementType = result.reviewedElementType;

        for (std::size_t index = 0; index < result.problems.size(); ++index) {
            const core::ProblemItem& problem = result.problems[index];
            const std::string cited =
                index < result.citedGuidelineIds.size() ? result.citedGuidelineIds[index] : problem.guideline_id;

            // The parser empties the guideline id of a finding citing an id this
            // pass was not given. Whether that is a stray citation or a genuinely
            // unknown one depends on whether another pass owns the id.
            if (problem.guideline_id.empty()) {
                const auto owner = owning_pass.find(cited);
                if (owner != owning_pass.end() && owner->second != pass.pass_id) {
                    merged.discarded_findings.push_back(
                        std::format("{} cited {}, which pass '{}' reviews; the finding was discarded.",
                                    label,
                                    cited,
                                    owner->second));
                    continue;
                }
                const std::string key =
                    cited.empty() ? problem.element_id + "\n" + problem.message : cited + "\n" + problem.element_id;
                const auto first = unknown_reference_first_pass.try_emplace(key, pass.pass_id).first;
                if (first->second != pass.pass_id)
                    continue;
            }

            merged.merged.problems.push_back(problem);
            merged.merged.citedGuidelineIds.push_back(cited);
            merged.merged.findingConfidences.push_back(
                index < result.findingConfidences.size() ? result.findingConfidences[index] : std::string{});
            merged.merged.suggestedElementTexts.push_back(
                index < result.suggestedElementTexts.size() ? result.suggestedElementTexts[index] : std::string{});
            merged.merged.proposedOperations.push_back(index < result.proposedOperations.size()
                                                           ? result.proposedOperations[index]
                                                           : std::vector<core::reviews::PatchOperation>{});
        }
        merged.merged.rejectedOperationReasons.insert(merged.merged.rejectedOperationReasons.end(),
                                                      result.rejectedOperationReasons.begin(),
                                                      result.rejectedOperationReasons.end());
    }

    if (!merged.any_succeeded()) {
        std::string joined;
        for (const std::string& error : merged.pass_errors)
            joined += (joined.empty() ? "" : " ") + error;
        merged.merged.errorMessage = joined.empty() ? std::string("No review pass ran.") : joined;
    }
    return merged;
}

std::string CombinePassPrompts(const std::vector<SccgReviewPassRequest>& passes) {
    std::string combined;
    for (std::size_t index = 0; index < passes.size(); ++index) {
        if (index > 0)
            combined += "\n";
        combined += Separator(index, passes.size(), passes[index].pass_id) + "\n" + passes[index].request.prompt;
    }
    return combined;
}

bool SplitPassPrompts(const std::string& combined, std::vector<SccgReviewPassRequest>& passes) {
    if (passes.empty())
        return false;

    // Every separator must still be there, in order, each opening its own line.
    std::vector<std::size_t> starts;
    std::size_t search_from = 0;
    for (std::size_t index = 0; index < passes.size(); ++index) {
        const std::string separator = Separator(index, passes.size(), passes[index].pass_id);
        const std::size_t found = combined.find(separator, search_from);
        if (found == std::string::npos || (found > 0 && combined[found - 1] != '\n'))
            return false;
        starts.push_back(found);
        search_from = found + separator.size();
    }

    std::vector<std::string> prompts;
    for (std::size_t index = 0; index < passes.size(); ++index) {
        const std::string separator = Separator(index, passes.size(), passes[index].pass_id);
        std::size_t body_start = starts[index] + separator.size();
        if (body_start < combined.size() && combined[body_start] == '\n')
            ++body_start;
        std::size_t body_end = index + 1 < passes.size() ? starts[index + 1] : combined.size();
        // The newline CombinePassPrompts put before the next separator.
        if (index + 1 < passes.size() && body_end > body_start && combined[body_end - 1] == '\n')
            --body_end;
        prompts.push_back(body_end > body_start ? combined.substr(body_start, body_end - body_start) : std::string{});
    }

    for (std::size_t index = 0; index < passes.size(); ++index)
        passes[index].request.prompt = std::move(prompts[index]);
    return true;
}

} // namespace review
