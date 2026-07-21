#pragma once

// Phase 9 Stage 5: the edit seam onto the library-owned document.
//
// Stage 4 made the library the *load* source of truth: `AppState` retains a
// `LibraryDocument` and projects `loaded_case` from it. Editing still runs on
// the legacy models (`parser::AssuranceCase` + `sacm::AssuranceCasePackage`)
// through `core::element_factory`, which the audit log serializes and hashes
// and the replayer re-applies. Flipping all of that at once is Stage 6.
//
// This seam is the incremental bridge: one library mutation Operation at a
// time, each proven to reproduce the corresponding legacy edit before anything
// depends on it. The functions here translate an app-level edit into a
// `sacm::commands::Operation`, apply it to the opaque `LibraryDocument`, and
// flatten the result to strings so no library type crosses into `core`.
//
// Only operations whose library mapping is proven equivalent to the legacy
// mutator live here. `SetName` is first: it maps 1:1 to clause 8.6's single
// name LangString for the primary language. Multi-language names, the
// statement-vs-note Description split, compound add-child, reparenting delete,
// and ACP tagging are later slices — they are deliberately absent rather than
// approximated.

#include "sacm_adapter/library_load.h"  // LibraryDocument, LoadDiagnostic

#include <string>
#include <vector>

namespace sacm_adapter {

// Result of applying one edit Operation to a LibraryDocument. `applied` is the
// library's own apply flag; on failure the document is unchanged and
// `diagnostics` explain why (reusing LoadDiagnostic's flattened shape).
struct EditOutcome {
    bool applied = false;
    std::vector<LoadDiagnostic> diagnostics;
};

// Sets an element's name for one language, mirroring
// `core::SetElementTextField(ElementTextField::Name, ...)`. `language` is
// typically "en"; SACM's name is a single LangString (clause 8.6), so a
// non-primary language overwrites the stored name rather than accumulating a
// map the way the legacy POD does — callers editing secondary-language names
// must not rely on this until that impedance is handled in a later slice.
EditOutcome apply_set_name(LibraryDocument& document, const std::string& element_id,
                           const std::string& name, const std::string& language);

} // namespace sacm_adapter
