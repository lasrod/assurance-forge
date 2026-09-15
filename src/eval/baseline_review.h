#pragma once

// A review of one assurance-case element without SCCG, for the evaluation
// harness's --baseline mode.
//
// Paper B compares SCCG-guided review with the AI review a team would build
// without SCCG. The comparison only measures SCCG if everything else is held
// fixed, so the baseline request carries the same system instruction, the same
// selected element and the same surrounding argument an SCCG review's data
// packages carry -- and nothing from the catalogue: no guidelines, profiles,
// passes, rules for unavailable data, pre-checks, or SCCG response schema.
//
// It lives beside the harness rather than in `review/` because nothing else
// sends it: it is the control of a measurement, not a review method the
// application offers.

#include "core/assurance_tree.h"
#include "parser/xml_parser.h"

#include <string>
#include <vector>

namespace eval {

// How much of the argument a baseline request carries.
enum class BaselineContext {
    // The selected element and the surrounding argument an SCCG review's data
    // packages carry: the control for SCCG itself.
    SurroundingArgument,
    // The selected element alone, as a claim pasted into a chat tool: the
    // control for what supplying the argument contributes.
    ElementOnly,
};

// Named in every baseline record, so a change to the wording is a new version
// rather than a silent change to what an existing sweep measured.
const char* BaselinePromptVersion(BaselineContext context);

struct BaselineReviewRequest {
    std::string system_instruction;
    std::string prompt;
    // `prompt` in two pieces that concatenate to it: the instruction every
    // baseline review shares, then this element's data, so a provider can cache
    // the first across a sweep.
    std::vector<std::string> prompt_segments;
    // The selected element and every element the surrounding argument carries,
    // the same set an SCCG review of this element reads.
    std::vector<std::string> reviewed_element_ids;
};

// False, with `out_error` saying why, for an element that is not found or that
// AI review does not support -- the same refusals an SCCG review makes.
bool BuildBaselineReviewRequest(const parser::AssuranceCase& assurance_case,
                                const core::AssuranceTree& tree,
                                const std::string& element_id,
                                BaselineReviewRequest& out_request,
                                std::string& out_error,
                                BaselineContext context = BaselineContext::SurroundingArgument);

struct BaselineFinding {
    // The model's message, why it matters, its suggested fix and its
    // confidence, laid out as an SCCG finding's message is, so a judge reading
    // findings from both setups is not shown a difference in format.
    std::string message;
    std::string confidence;
};

struct BaselineReviewParseResult {
    // Empty means the response parsed.
    std::string error_message;
    std::string reviewed_element_id;
    std::vector<BaselineFinding> findings;
};

BaselineReviewParseResult ParseBaselineReviewResponse(const std::string& response_text);

} // namespace eval
