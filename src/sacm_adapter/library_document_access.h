#pragma once

// Internal: the only place the opaque LibraryDocument is opened up.
//
// Not installed and not includable from `core`, `ui`, or `app` — those layers
// see LibraryDocument as a handle. Adapter translation units include this to
// reach the underlying library document.

#include "sacm_adapter/library_load.h"

#include "sacm/model/document.h"

namespace sacm_adapter {

struct LibraryDocument::Impl {
    sacm::model::Document document;
};

struct LibraryDocumentAccess {
    static const sacm::model::Document& document(const LibraryDocument& wrapper);
    // Mutable access for the edit seam (Phase 9 Stage 5). Confined to adapter
    // translation units, same as the const accessor, so `sacm::model` still
    // never leaves this layer.
    static sacm::model::Document& mutable_document(LibraryDocument& wrapper);
    // Document is move-only, so construction goes through here rather than a
    // constructor taking a library type (which would put sacm::model in the
    // public header this file exists to keep it out of).
    static void set_document(LibraryDocument& wrapper, sacm::model::Document&& document);
};

} // namespace sacm_adapter
