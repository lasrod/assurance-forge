#pragma once

// Projects a library-owned SACM document into the flat POD model the
// application currently renders from.
//
// This is the direction Phase 9 depends on: once the library owns the document,
// `core::AssuranceCase` becomes a derived view rather than a parse result. In
// Stage 3 the projection is only compared against the legacy parser; nothing
// consumes it yet.
//
// Note this is a lossy, *deliberate* narrowing. The POD model carries what the
// UI draws; the library keeps everything the standard defines. Elements the
// application has no concept of are simply absent here — which is fine as long
// as the library, not this projection, is what gets serialized.

#include "core/sacm_model.h"
#include "sacm/sacm_model.h"

#include <vector>

namespace sacm_adapter {

class LibraryDocument;

core::AssuranceCase project_case(const LibraryDocument& document);

// Phase 9 Stage 6: projects the library document's terminology and artifact
// packages into the legacy structs the audit's canonical hash covers. The flat
// `project_case` POD deliberately omits these container elements; the
// library-backed package projection (`core::project_library_package`) needs
// them so audit coverage is not reduced when the audit readers move onto the
// library. Placement mirrors the library model (both at the case-package
// level); consistency between the audit's two sides is what matters, not the
// original nesting.
std::vector<sacm::TerminologyPackage> project_terminology_packages(const LibraryDocument& document);
std::vector<sacm::ArtifactPackage> project_artifact_packages(const LibraryDocument& document);

} // namespace sacm_adapter
