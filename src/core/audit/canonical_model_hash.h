#pragma once

#include "sacm/sacm_model.h"

#include <string>

// Deterministic content hash of the logical SACM model, independent of XML
// formatting (design §12). Two SACM files with equivalent logical content but
// different whitespace, attribute order, or namespace serialization must
// produce the same hash.
//
// Returned value is the lowercase hex sha256 of a canonical textual encoding
// of the package; the canonical encoding itself is an implementation detail
// and may change between major schema versions of this hash (bump
// `kCanonicalModelHashVersion` if it does).
namespace core::audit {

inline constexpr int kCanonicalModelHashVersion = 1;

std::string CanonicalModelHash(const sacm::AssuranceCasePackage& package);

// Exposed for testing / diagnostics. Not stable across versions.
std::string CanonicalModelText(const sacm::AssuranceCasePackage& package);

} // namespace core::audit
