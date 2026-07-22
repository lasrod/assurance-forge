#pragma once

// Terminology-display passes over the derived render views (Phase 9 Stage 7).
//
// The POD `parser::AssuranceCase` the UI draws is a projection of the
// library-owned document; after projecting it, these passes adjust how
// terminology renders -- a term shows as inline underlined clickable text rather
// than a drawn context node. Extracted here (from app_state) so both the load
// path and the upcoming library-primary edit path apply the same passes.

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace core {

// Remove terminology artifact references (and the contexts sourcing them) that
// are not a *visible* terminology context from the render model, so a term shows
// as inline underlined clickable text rather than a drawn context node.
void HideTerminologyArtifactReferences(parser::AssuranceCase& model,
                                       const sacm::AssuranceCasePackage& package);

// Repoint each visible terminology artifact reference's display fields at its
// term, so the inline term chip renders the term's label and definition.
void RefreshVisibleTerminologyContextDisplay(parser::AssuranceCase& model,
                                             const sacm::AssuranceCasePackage& package);

// A GSN strategy is created as an ArgumentReasoning carrying a `strategyTarget`
// tag (the goal it will support) with no inference until its first sub-goal
// materializes one (the deferred single-inference encoding). Such a *bare*
// strategy has no relationship to place it under its goal, so synthesize a
// sourceless placeholder AssertedInference `{reasoning=strategy, target=goal}`
// into the render model -- the POD tree tolerates a sourceless inference and
// renders the strategy under its goal. This is a **render-only** step applied to
// the render model, NOT the projection (which feeds the saved package + audit
// hash), so the placeholder is never serialized. Once a real inference
// (reasoning=strategy) exists, none is synthesized.
void SynthesizeBareStrategyPlacements(parser::AssuranceCase& model,
                                      const sacm::AssuranceCasePackage& package);

} // namespace core
