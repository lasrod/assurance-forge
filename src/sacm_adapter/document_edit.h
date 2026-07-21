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
// depends on it. `apply_text_edit` translates an app text edit into a
// `sacm::commands::Operation`, applies it to the opaque `LibraryDocument`, and
// flattens the result to strings so no library type crosses into `core`.
//
// Only (field, element-kind) combinations whose library mapping is proven
// equivalent to `core::SetElementTextField` are wired; the rest report
// `supported == false` rather than being approximated. The mapping mirrors the
// projection (src/sacm_adapter/case_projection.cpp):
//
//   * Name    -> SetName (clause 8.6 single name LangString, primary language).
//   * Content -> SetDescription on a Claim/ArgumentReasoning: the app's
//     `content` is the element's primary Description (clause 8.9), which the
//     library's SetDescription edits.
//
// Deliberately not yet mapped (later slices, some needing new library
// operations): a Claim's secondary-note Description, Term/Expression `value`,
// and multi-language names/Descriptions that exceed a single LangString.

#include "sacm_adapter/library_load.h"  // LibraryDocument, LoadDiagnostic

#include <string>
#include <vector>

namespace sacm_adapter {

// The app text field being edited, mirroring core::ElementTextField without
// depending on `core` (this layer sits below it). The caller maps between the
// two.
enum class TextField {
    Name,
    Content,
    Description,
};

// Result of applying one text edit to a LibraryDocument.
//   * supported == false: this (field, element-kind) has no library mapping
//     yet, so nothing was attempted and the document is unchanged. Not an
//     error -- the caller keeps the legacy edit authoritative and leaves the
//     library document untouched for this field until a later slice wires it.
//   * supported == true, applied == false: the library rejected the operation;
//     the document is unchanged and `diagnostics` explain why.
//   * supported == true, applied == true: the edit was applied.
struct EditOutcome {
    bool supported = true;
    bool applied = false;
    std::vector<LoadDiagnostic> diagnostics;
};

// Applies a text edit mirroring
// `core::SetElementTextField(field, language, value)`. `language` is typically
// "en"; SACM's name is a single LangString (clause 8.6), so a non-primary
// language overwrites the stored name rather than accumulating a map the way
// the legacy POD does -- callers editing secondary-language names must not rely
// on this until that impedance is handled in a later slice.
EditOutcome apply_text_edit(LibraryDocument& document, const std::string& element_id,
                            TextField field, const std::string& language, const std::string& value);

} // namespace sacm_adapter
