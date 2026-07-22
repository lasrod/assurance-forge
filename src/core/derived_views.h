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

} // namespace core
