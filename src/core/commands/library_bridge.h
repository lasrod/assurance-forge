#pragma once

#include "core/commands/command_bus.h"
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
using LibraryBridgeMutator =
    std::function<bool(parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& error)>;

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
// or if the reload cannot read the serialization. `rederive_failure_context`, when
// given, is appended to the re-derive failure message ("... failed at transaction 3
// event 1") so a replayer can say WHERE in the log the failure was.
//
// This is the ONLY implementation. The audit replayer's `BridgeViaLegacy` used to
// be a second copy of the same algorithm; when this one was fixed to stop dropping
// vendor content, the copy kept the defect and relocated it to the restore path,
// where a recovery silently destroyed every Assurance Claim Point. They are one
// function now so they cannot drift apart again.
bool BridgeLegacyMutationToLibrary(sacm_adapter::LibraryDocument& document,
                                   const LibraryBridgeMutator& mutate,
                                   std::string& error,
                                   std::string_view rederive_failure_context = {});

// The single chokepoint every audited command uses to become library-primary.
// When a library document is present and the flip is allowed, `mutate` runs on a
// scratch projection of the library (via BridgeLegacyMutationToLibrary) and
// `ctx.library_primary` is set, so the command bus derives what it saves/hashes from
// the library and the app re-derives the live views at the frame boundary. Otherwise
// (legacy-parser fallback, no-bus dispatch, or the kill switch) `mutate` runs in place
// on the caller's `ctx.model`/`ctx.package`, exactly as before. `mutate` sees the same
// legacy (model, package) signature either way, so a command flips by wrapping its
// existing mutator once -- reproducing the legacy result, which the audit replay also
// reproduces, so live and replay converge by construction.
bool ApplyLibraryPrimaryOrLegacy(CommandContext& ctx, const LibraryBridgeMutator& mutate, std::string& error);

} // namespace core::commands
