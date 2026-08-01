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
    std::unique_ptr<LibraryDocument> document; // null when the load failed
    std::vector<LoadDiagnostic> diagnostics;
    std::string source_namespace;
    std::string source_version; // "2.3", "2.2", ... or "unknown"
    bool ok = false;
};

// Loads a SACM file through the library in tolerant mode, which is what the
// application must use: real project files include pre-2.3 revisions and
// third-party dialects.
LoadOutcome load_document(const std::filesystem::path& path);

// One-line human-readable summary of load diagnostics ("code/severity: message"
// entries joined by "; "), for surfacing the library's root-cause in an error
// message when a load fails. Empty when there are no diagnostics.
std::string summarize_load_diagnostics(const std::vector<LoadDiagnostic>& diagnostics);

// Re-derives `document` in place from serialized SACM XML, tolerantly. This is
// the Phase 9 Stage 5 safety net: after an audited edit that was not routed
// through a library operation, the application rebuilds the library document
// from the authoritative legacy package's serialization so it never drifts.
// Returns false (and leaves `document` unchanged) if the XML could not be
// loaded. The wrapper identity is preserved -- only the contained document is
// replaced -- so existing handles stay valid.
bool reload_document(LibraryDocument& document, std::string_view xml);

// Re-derives `document` from `xml` like `reload_document`, then restores onto
// the result the compatibility content the outgoing document carried --
// preserved vendor elements, preserved vendor attributes, and the foreign
// namespace declarations they need to stay readable.
//
// Use this whenever `xml` came from a projection of the very document being
// replaced. The legacy `sacm::AssuranceCasePackage` has no field for unknown
// XML, so serializing through it silently drops everything a tolerant load
// preserved; the content is still in the outgoing document, and this puts it
// back. Round-trip integrity for assurance-case data is a hard project
// constraint, and a bridged edit that quietly erases a vendor's data on save
// breaks it just as surely as a lossy save does.
//
// Returns false (leaving `document` unchanged) if the XML could not be loaded.
bool reload_document_keeping_compatibility_content(LibraryDocument& document, std::string_view xml);

// Result of serializing a library document to SACM XMI.
struct SaveOutcome {
    bool ok = false;
    std::string xml;
    std::vector<LoadDiagnostic> diagnostics;
};

// Serializes `document` to SACM XMI. Phase 9 Stage 6 (library-backed save) uses
// tolerant mode so preserved vendor/compatibility content (e.g. layout carried
// on import) survives -- round-trip integrity is a hard project constraint.
// Strict mode is the clean SACM 2.3 export and refuses such content
// (SACM-XMI-006); use it only for an explicit "export strict" action.
SaveOutcome save_document(const LibraryDocument& document, bool tolerant = true);

// Builds the seed document a new SACM file starts from -- an
// AssuranceCasePackage named `case_name` holding one ArgumentPackage and one
// Claim -- and serializes it in STRICT mode.
//
// This exists so the seed is produced by the same writer that performs every
// save. It used to be a hand-written literal in `core`, in the SACM 2.2
// namespace and using `id=` rather than `xmi:id`; the tolerant reader accepted
// it, so nothing looked broken, but a new project began as a document no strict
// consumer would take and the first save silently rewrote it. Strict mode is
// deliberate: a brand-new document carries nothing to preserve, so anything
// strict would refuse is a defect in this function.
SaveOutcome new_case_document_xmi(std::string_view case_name);

} // namespace sacm_adapter
