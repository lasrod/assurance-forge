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

namespace sacm_adapter {

class LibraryDocument;

core::AssuranceCase project_case(const LibraryDocument& document);

} // namespace sacm_adapter
