#include "review/sccg/sccg_prechecks.h"

#include "core/sccg/staged_checks.h"

#include <algorithm>
#include <string>
#include <vector>

namespace review::sccg {
namespace {

// Which pre-checks the tool can actually decide. A pre-check outside this set
// is reported unavailable rather than clean: SCCG names five, and a review that
// showed a silent row for one nobody implemented would be claiming a check it
// never ran.
bool IsDecidable(const std::string& precheck_id) {
    static const std::vector<std::string> decidable{"check-explicit-strategy",
                                                    "check-evidence-trace",
                                                    "check-evidence-citation-precision",
                                                    "check-evidence-control-attributes",
                                                    "check-evidence-state-fixed"};
    return std::find(decidable.begin(), decidable.end(), precheck_id) != decidable.end();
}

} // namespace

std::vector<PrecheckResult> RunPrechecks(const parser::GuidelinesDocument& catalog,
                                         const parser::AssuranceCase& model,
                                         const core::AssuranceTree& tree,
                                         const std::string& element_id) {
    std::vector<PrecheckResult> results;
    if (catalog.prechecks.empty() || element_id.empty()) {
        return results;
    }

    // One implementation of each check, shared with the staged-draft guardrail.
    // Scoping it to the reviewed element is what makes it a review pre-check
    // rather than a sweep: the profile decides what is under review, and a
    // finding about a neighbour is not this review's subject.
    (void)tree;
    const std::vector<core::sccg::StagedFinding> findings = core::sccg::CheckStagedArgument(model, {element_id});

    for (const parser::Precheck& precheck : catalog.prechecks) {
        PrecheckResult result;
        result.precheck_id = precheck.id;
        result.display_name = precheck.display_name;
        result.guideline_ids = precheck.related_guideline_ids;
        result.result_type = precheck.result_type;
        result.interpretation = precheck.interpretation;

        if (!IsDecidable(precheck.id)) {
            result.unavailable = true;
            results.push_back(std::move(result));
            continue;
        }

        for (const core::sccg::StagedFinding& finding : findings) {
            if (finding.check_id != precheck.id || finding.element_id != element_id) {
                continue;
            }
            result.candidate = true;
            result.detail = finding.detail;
            break;
        }
        results.push_back(std::move(result));
    }

    return results;
}

} // namespace review::sccg
