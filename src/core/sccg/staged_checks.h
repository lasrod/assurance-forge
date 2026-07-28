#pragma once

// SCCG checks that can be decided mechanically, run against what an agent has
// staged so it can correct itself before a person ever reads it.
//
// **The honest scope of this file.** Most SCCG guidance is prose that only a
// reader -- human or model -- can judge: whether a claim is *sufficiently*
// justified, whether evidence is *relevant*, whether a decomposition is
// *complete*. None of that is decidable here, and pretending otherwise would be
// worse than not checking, because a green result would read as conformance.
//
// What is decidable is structural: a solution with children, a strategy that
// develops into nothing, an unsupported claim that is not marked undeveloped, a
// cycle in the support graph. Each check below names the guideline it serves,
// and the set is deliberately small and individually tested. Everything else
// reaches the agent as advisory context through the SCCG resources and prompts
// the MCP server publishes, and is judged by the reviewer.
//
// Findings never block acceptance. The reviewer is the authority on a safety
// argument; this is here to stop an agent wasting their time with a shape that
// was obviously wrong.

#include "core/sacm_model.h"

#include <string>
#include <vector>

namespace core::sccg {

enum class FindingSeverity {
    // Something SCCG advises against. Worth the agent reconsidering.
    Advisory,
    // Structurally wrong in a way a reviewer will certainly reject.
    Problem,
};

const char* FindingSeverityToString(FindingSeverity severity);

struct StagedFinding {
    // The SCCG guideline this serves, e.g. "AR-02". Carried so a reviewer can
    // look up the rule rather than take the tool's word for it.
    std::string     guideline_id;
    // The guideline's own wording, so the finding is readable without the
    // catalog to hand.
    std::string     statement;
    // What is wrong with this particular element.
    std::string     detail;
    std::string     element_id;
    FindingSeverity severity = FindingSeverity::Advisory;
};

// Checks the argument a change set would produce. Takes the preview rather than
// the operations because that is the thing a reviewer will see, and because a
// structural rule is a property of the resulting graph rather than of the edit
// that made it.
//
// `changed_element_ids` limits reporting to what this change set touched: an
// agent should not be handed findings about parts of the argument it did not
// write, which are the user's business and not the agent's to fix.
std::vector<StagedFinding> CheckStagedArgument(const parser::AssuranceCase&    preview,
                                               const std::vector<std::string>& changed_element_ids);

} // namespace core::sccg
