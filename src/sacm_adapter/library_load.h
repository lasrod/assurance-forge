#pragma once

// Assurance Forge's seam onto the independent SACM 2.3 library.
//
// Phase 9 migrates the application to the library as its source of truth. Stage
// 3 is the passive step: load through the library *alongside* the existing
// parser and compare, changing nothing about save or export. The differences
// are the product of this stage -- they say where the projection would lose
// data before anything depends on it.
//
// LibraryDocument is deliberately opaque. `src/core/app_state.h` already holds
// a `sacm::AssuranceCasePackage` from the *legacy* `namespace sacm`; exposing
// `sacm::model::Document` alongside it would merge that namespace with the
// library's, leaving `sacm::AssuranceCasePackage` and
// `sacm::model::AssuranceCasePackage` visible in one header. Keeping the type
// behind a pointer means no `sacm::model` name ever enters `core`, which is
// also the shape Stage 4 wants when AppState starts owning the document.

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sacm_adapter {

// One diagnostic from the library, flattened to strings so no library type
// crosses this boundary.
struct LoadDiagnostic {
    std::string code;
    std::string severity;
    std::string message;
};

class LibraryDocument {
  public:
    LibraryDocument();
    ~LibraryDocument();
    LibraryDocument(LibraryDocument&&) noexcept;
    LibraryDocument& operator=(LibraryDocument&&) noexcept;
    LibraryDocument(const LibraryDocument&) = delete;
    LibraryDocument& operator=(const LibraryDocument&) = delete;

  private:
    friend struct LibraryDocumentAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct LoadOutcome {
    std::unique_ptr<LibraryDocument> document;  // null when the load failed
    std::vector<LoadDiagnostic> diagnostics;
    std::string source_namespace;
    std::string source_version;  // "2.3", "2.2", ... or "unknown"
    bool ok = false;
};

// Loads a SACM file through the library in tolerant mode, which is what the
// application must use: real project files include pre-2.3 revisions and
// third-party dialects.
LoadOutcome load_document(const std::filesystem::path& path);

// Re-derives `document` in place from serialized SACM XML, tolerantly. This is
// the Phase 9 Stage 5 safety net: after an audited edit that was not routed
// through a library operation, the application rebuilds the library document
// from the authoritative legacy package's serialization so it never drifts.
// Returns false (and leaves `document` unchanged) if the XML could not be
// loaded. The wrapper identity is preserved -- only the contained document is
// replaced -- so existing handles stay valid.
bool reload_document(LibraryDocument& document, std::string_view xml);

} // namespace sacm_adapter
