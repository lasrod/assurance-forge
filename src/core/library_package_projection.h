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

#include "sacm/sacm_model.h"

namespace sacm_adapter {
class LibraryDocument;
}

namespace core {

sacm::AssuranceCasePackage project_library_package(const sacm_adapter::LibraryDocument& document);

} // namespace core
