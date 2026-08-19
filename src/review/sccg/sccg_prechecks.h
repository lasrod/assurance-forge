#pragma once

// Step 4 of the SCCG review workflow: the deterministic pre-checks a tool can
// run before human or AI judgment.
//
// SCCG publishes these in `dist/prechecks.json`, and both catalog parsers have
// read them into `GuidelinesDocument::prechecks` since the day they were
// written. Nothing had ever read that vector, so the review method skipped a
// stage the guideline describes and the model was asked to judge without the
// signals a tool can decide for it.
//
// **Two registries, deliberately not merged.** A guideline's
// `tool.suggested_checks` names a check a tool *might* implement; `prechecks`
// is the published set with an `expected_data` contract and a stated
// `result_type`. The catalog gives some of them the same id, which is how a
// finding and a pre-check line up -- but they answer to different schemas, and
// a `suggested_checks` id is not a pre-check just because a check exists for
// it. Only ids in the `prechecks` registry are reported here.
//
// **This adds no checking.** The verdicts come from
// `core::sccg::CheckStagedArgument`, the same implementation that guards staged
// drafts, so a rule cannot mean one thing to an agent staging work and another
// to a review of the same element. What this adds is the registry's framing:
// which checks exist, what each one expects, and SCCG's own words for how far
// its result may be trusted.

#include "core/assurance_tree.h"
#include "parser/guidelines_parser.h"
#include "parser/xml_parser.h"

#include <string>
#include <vector>

namespace review::sccg {

// One published pre-check, and what it decided for the reviewed element.
struct PrecheckResult {
    // The `prechecks.json` id, e.g. "check-explicit-strategy".
    std::string precheck_id;
    std::string display_name;
    std::vector<std::string> guideline_ids;
    // "boolean_candidate" or "missing_fields", from the registry.
    std::string result_type;
    // SCCG's own sentence about how far the result may be trusted. Carried
    // rather than paraphrased: it is the difference between a candidate signal
    // and a finding, and the model is the one that has to observe it.
    std::string interpretation;
    // Whether the check fired. A pre-check the tool cannot decide is reported
    // as `unavailable` instead, because "did not fire" and "was never run" are
    // different facts and a reviewer must not read one as the other.
    bool candidate = false;
    bool unavailable = false;
    // What made it fire, where the check names one.
    std::string detail;
};

// Runs every published pre-check against one element. `catalog` supplies the
// registry; a catalog with no `prechecks` section yields nothing, which is
// honest -- there is no local list to fall back on.
std::vector<PrecheckResult> RunPrechecks(const parser::GuidelinesDocument& catalog,
                                         const parser::AssuranceCase& model,
                                         const core::AssuranceTree& tree,
                                         const std::string& element_id);

} // namespace review::sccg
