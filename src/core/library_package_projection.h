#pragma once

// Phase 9 Stage 6: projects a library-owned document into the legacy
// `sacm::AssuranceCasePackage` the audit subsystem hashes.
//
// The audit verifier compares the on-disk model against the event-log replay by
// canonical hash. Both sides must derive their package the *same* way, or the
// projection-vs-legacy baseline (where the library is more correct) would make
// them diverge. Routing both the on-disk read and the replayed-side
// normalization through this single projection makes them converge by
// construction, independent of that baseline -- that is the key to keeping the
// audit green once the saved bytes become library XMI (Approach B).
//
// Scope note: this currently projects the argument content (claims, reasonings,
// artifact references, asserted relationships) via the proven `project_case`
// POD and `RebuildSacmArgumentPackageFromParser`. Terminology and artifact
// packages -- which the canonical hash also covers -- are added before this is
// wired into the audit readers, so audit coverage is never reduced.

#include "legacy_sacm/sacm_model.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sacm_adapter {
class LibraryDocument;
}

namespace core {

sacm::AssuranceCasePackage project_library_package(const sacm_adapter::LibraryDocument& document);

// The faithful projection the application derives `sacm_package` from at load.
// A library-XMI file cannot be read into a complete package by the legacy parser
// (it reads as near-empty), so the app must reconstruct the package from the
// library instead. Unlike the audit projection (project_library_package) -- which
// may collapse multiple argument packages as long as it does so consistently on
// both audit sides -- this preserves the argument-package structure (one
// sacm::ArgumentPackage per library package, with identity and purpose tags) and
// carries the fields the POD drops: vendor TaggedValues (ACP, confidence-package,
// GSN-role) and each artifact reference's referencedArtifact (terminology
// detection). Kept separate from the audit projection so the audit canonical
// hash is unchanged.
sacm::AssuranceCasePackage project_library_package_with_tags(const sacm_adapter::LibraryDocument& document);

// Phase 9 Stage 6: the canonical model hash computed by loading the model
// through the library and projecting it back to a package before hashing. Every
// audit canonical-hash site routes through this so all sides -- the manifest
// cache, the replayed model, the on-disk model, and snapshots -- derive the
// hash identically and therefore converge, independent of the projection-vs-
// legacy baseline (the on-disk / replayed reader coupling that a naive
// library-backed save would break). Returns nullopt when the input cannot be
// loaded through the library, so callers keep their existing failure handling.
std::optional<std::string> library_canonical_hash_from_xml(std::string_view xml);
std::optional<std::string> library_canonical_hash(const sacm::AssuranceCasePackage& package);
std::optional<std::string> library_canonical_hash_from_file(const std::filesystem::path& path);

// Phase 9 Stage 6: serializes a package to library SACM XMI, by loading it
// through the library and saving it back out in tolerant mode.
//
// **LOSSY for unknown content -- FALLBACK ONLY.** Since Stage 7 every save site
// that has a `sacm_adapter::LibraryDocument` describing the state to persist
// serializes THAT document (`sacm_adapter::save_document`), which re-emits the
// unknown/foreign XML and vendor attributes a tolerant load preserved. This
// function stays for the cases where no such document exists (or where the
// state to persist lives only in a package -- the strategy migration normalizes
// the projection, not the document): its input is a *legacy*
// `sacm::AssuranceCasePackage`, which has no field for unknown content (see
// src/sacm/sacm_model.h), so anything the structs cannot model is dropped here.
// It preserves everything they do model, ACP and other TaggedValues (clause
// 8.12) included. Returns nullopt if the library round-trip fails, so callers
// can fall back to the legacy serialization.
std::optional<std::string> library_xmi_from_package(const sacm::AssuranceCasePackage& package);

} // namespace core
