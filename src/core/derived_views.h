#pragma once

// The re-derive engine (Phase 9 Stage 7): rebuilds the two legacy render views --
// the POD `parser::AssuranceCase` the UI draws and the `sacm::AssuranceCasePackage`
// the app's ACP/terminology services read -- from the library-owned document.
//
// Since the library is the source of truth, these views are derived: projected
// from the library and refreshed for terminology display. The same re-derive runs
// at load and after a library-primary edit, so the views never drift from the
// library.

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

namespace sacm_adapter {
class LibraryDocument;
}

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

// Project `library` into the two legacy render views and apply the terminology-
// display passes. This is the single re-derive both the load path and the
// library-primary edit path use, so they cannot diverge.
void RebuildDerivedViewsFromLibrary(const sacm_adapter::LibraryDocument& library,
                                    parser::AssuranceCase& out_model,
                                    sacm::AssuranceCasePackage& out_package);

} // namespace core
