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

#include <string>
#include <string_view>
#include <vector>

namespace sacm_adapter {

class LibraryDocument;

core::AssuranceCase project_case(const LibraryDocument& document);

// One element as the LIBRARY holds it, before any projection decision. Exists so
// the projection's completeness can be measured against the document rather than
// asserted: `project_case` deliberately omits packages (containers, not drawn
// nodes) and utility elements (clause 8.7 metadata carried ON elements), and
// those two exclusions are the only ones it is entitled to make.
struct DocumentElement {
    std::string id;
    std::string kind; // lowercased SACM class name, matching SacmElement::type
    bool is_package = false;
    bool is_utility = false;
    // Stable rendering of the element's SACM attributes and reference ends, as
    // `name=value;` pairs in a fixed order.
    //
    // Exists so a loss sweep can compare an element with itself across a round
    // trip instead of only counting kinds. Counting kinds cannot see an
    // attribute going missing from an element that survives -- which is exactly
    // how `isCounter` was lost, turning a rebuttal into a supporting inference,
    // with the element still present and the inventory still balanced.
    std::string attributes;
};

// Every element in `document`, including the ones `project_case` filters out.
std::vector<DocumentElement> list_document_elements(const LibraryDocument& document);

// The SACM 2.3 class name for a POD `SacmElement::type`, which the projection
// writes as the lowercased class name. Callers that put an element kind in front
// of a user need the spec's spelling ("ArgumentGroup", not "argumentgroup") --
// it is what the standard, the file and the diagnostics catalogue all use.
// Returns `pod_type` unchanged when it matches no SACM class, so an extension or
// dialect type still prints as something rather than nothing.
std::string sacm_class_name_for_pod_type(std::string_view pod_type);

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

// Copies each library element's vendor TaggedValues onto the matching (by id)
// element of `package`, and restores each artifact reference's referencedArtifact
// (also dropped by the POD). The POD projection that builds `package` drops both,
// but the application stores ACP, confidence-package, and GSN-role data as tags
// on the legacy package and detects terminology via referencedArtifact, so a
// library-derived `sacm_package` must carry them or those features break after
// loading a library-XMI file (which the legacy parser reads as near-empty).
// Additive: existing tags on `package` are kept.
void copy_library_tags_onto_package(const LibraryDocument& document, sacm::AssuranceCasePackage& package);

// The per-argument-package grouping the faithful `sacm_package` projection needs.
// `project_case` flattens every package into one element list, which would
// collapse multiple argument packages -- ACP confidence arguments each live in
// their own package (see core::acp::IsConfidenceArgumentPackage) -- into a single
// unnamed package on load, losing the package identity and its purpose tag. Each
// shell carries a package's identity (id/gid/name/description, empty element
// lists) plus the ids of its argument elements in document order, so the caller
// can rebuild one sacm::ArgumentPackage per library package. Recurses into nested
// assurance case packages.
struct ArgumentPackageShell {
    sacm::ArgumentPackage identity;
    std::vector<std::string> element_ids;
};
std::vector<ArgumentPackageShell> project_argument_package_shells(const LibraryDocument& document);

} // namespace sacm_adapter
