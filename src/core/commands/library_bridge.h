#pragma once

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <functional>
#include <string>

namespace sacm_adapter {
class LibraryDocument;
}

namespace core::commands {

// A legacy model+package mutation -- the same shape every `core::*` mutator
// (SetElementTextField, AddChildElement, ...) already exposes.
using LibraryBridgeMutator = std::function<bool(parser::AssuranceCase& model,
                                                sacm::AssuranceCasePackage& package, std::string& error)>;

// Make a legacy mutation LIBRARY-PRIMARY without touching any live view. Projects
// a SCRATCH model+package from `document`, runs `mutate` on the scratch, then
// reloads `document` from the mutated scratch's serialization -- so the library
// becomes the source of truth for the edit and the caller's loaded_case/sacm_package
// are left untouched for the frame-boundary re-derive (the re-entrancy contract the
// command bus relies on). It reproduces the legacy result exactly, so a bridged live
// edit and the identically bridged audit replay converge by construction, with no
// migration. This is the LIVE twin of the replayer's BridgeViaLegacy, for commands
// whose native library seam is not (yet) audit-equivalent to the legacy mutator.
//
// Returns false and sets `error` if `mutate` fails (the library is then unchanged)
// or if the reload cannot read the serialization.
bool BridgeLegacyMutationToLibrary(sacm_adapter::LibraryDocument& document,
                                   const LibraryBridgeMutator& mutate, std::string& error);

} // namespace core::commands
