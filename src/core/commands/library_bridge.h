#pragma once

#include "core/commands/command_bus.h"
#include "parser/xml_parser.h"
#include "sacm_adapter/library_load.h" // LibraryDocument, LoadDiagnostic
#include "legacy_sacm/sacm_model.h"

#include <functional>
#include <string>
#include <vector>

namespace core::commands {

// A legacy model+package mutation -- the same shape every `core::*` mutator
// (SetElementTextField, AddChildElement, ...) already exposes.
using LibraryBridgeMutator =
    std::function<bool(parser::AssuranceCase& model, sacm::AssuranceCasePackage& package, std::string& error)>;

// Apply a legacy mutation when there is NO library document, and REFUSE when
// there is one.
//
// This used to be the guarded bridge: with a document present it projected a
// scratch model, ran the mutator on it and rebuilt the document from the result.
// That is gone. Every command applies through the library seams now, so reaching
// here with a document means the command met a shape no seam expresses -- and
// rebuilding the document from a projection is exactly the data loss the seams
// were built to stop. It fails, naming the command, and the document is untouched.
//
// The no-document branch remains for contexts that have never had one: the
// convergence tests build the legacy baseline that way.
bool ApplyLegacyOrRefuse(CommandContext& ctx, const LibraryBridgeMutator& mutate, std::string& error);

// True when this dispatch may apply a native library seam -- that is, when a
// document is present. Every command checks this first; a command that cannot
// express its shape through a seam refuses rather than falling back.
bool CanApplyLibraryPrimary(const CommandContext& ctx);

// Library diagnostics, surfaced verbatim (code/severity/message) so the
// application never has to reinterpret why the library refused an edit.
std::string FormatLibraryDiagnostics(const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics);

// The error a command reports when the library REJECTED a seam it supports:
// "The SACM library rejected <what>: <diagnostics>". A rejection is a hard
// failure, never a reason to quietly apply the legacy edit instead -- doing that
// would reinterpret a library refusal, which is the divergence this migration
// exists to remove.
std::string LibraryRejection(const std::string& seam, const std::vector<sacm_adapter::LoadDiagnostic>& diagnostics);

} // namespace core::commands
