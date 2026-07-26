#pragma once

// Internal class-name and kind-classification tables (not installed).
// Seeded from the concrete-class tables in
// docs/sacm/sacm-2.3-metamodel-inventory.md and asserted against it by
// test_metamodel_coverage.cpp.

#include "sacm/metadata/element_kind.h"

#include <optional>
#include <span>
#include <string_view>

namespace sacm::io::detail {

using metadata::ElementKind;

// Exact SACM 2.3 class-name lookup ("Claim" -> Claim). Also accepts the
// known ptc/22-03-13 alias ArtifactAssertedRelationship.
std::optional<ElementKind> kind_from_class_name(std::string_view name);

// Case-insensitive lookup for tolerant mode ("claim", "CLAIM").
std::optional<ElementKind> kind_from_class_name_ci(std::string_view name);

// Resolution of types from metamodels that specialize SACM.
//
// SACM is designed to be extended, and real interchange files exercise that:
// GSN's `Module`/`Goal`/`Strategy` are declared in a separate namespace with
// SACM classes as their `eSuperTypes`, so `xsi:type="gsn_:Goal"` denotes an
// element that *is a* SACM Claim. Resolving it to its SACM supertype is what
// lets such a file load as SACM at all; refusing to would leave the entire
// argument unread. No extension vocabulary reaches the public API -- the
// element becomes a Claim, and its original type is preserved as data so the
// information is not lost.
//
// Sourced from the extension metamodels' own eSuperTypes declarations, not
// inferred from names.
struct ExtensionType {
    // Aliases the `namespace_uri` argument passed to `resolve_extension_type`,
    // which is typically a temporary from prefix resolution. Copy it before the
    // argument dies -- unlike the other views here, it does not point into the
    // static table.
    std::string_view namespace_uri;
    std::string_view type_name;
    // The concrete SACM class this type specializes, if there is one.
    std::optional<ElementKind> sacm_kind;
    // True when the extension's relationship runs opposite to its SACM
    // supertype's and the endpoints must be swapped on import. GSN's
    // SupportedBy reads "A is supported by B" (source = the conclusion),
    // while SACM's AssertedInference reads "A infers B" (source = the
    // premise). Both store endpoints in the same inherited slots, so
    // resolving the type without swapping silently reverses every inference
    // in the argument.
    bool reverse_endpoints = false;
    // The SACM AssertionDeclaration literal this GSN type carries, when the GSN
    // transformation defines one ("assumed" for Assumption, "axiomatic" for
    // Justification). Empty when the type implies no declaration.
    //
    // Without it, Goal, Assumption and Justification all resolve to a bare
    // `Claim` and become indistinguishable -- GSN Metamodel v2.2 states a
    // transformation *must* apply the appropriate declaration. Carried as the
    // literal rather than the enum so this header stays free of the model
    // headers; the reader parses it with `model::parse_assertion_declaration`.
    std::string_view assertion_declaration;
};

// Resolves an xsi:type from a known SACM-extension namespace. Returns nullopt
// when the namespace is not a recognized extension. A recognized type whose
// SACM supertype is *abstract* resolves with an empty `sacm_kind`: it is a
// genuine SACM extension point with no concrete equivalent, and the caller must
// preserve rather than guess.
std::optional<ExtensionType> resolve_extension_type(std::string_view namespace_uri,
                                                    std::string_view type_name);

// Reserved TaggedValue key recording the extension type an element was read
// from, e.g. "{http://scsc.acwg.gsn/2.0}Goal". Written on import whenever an
// extension type resolves to a SACM supertype, because that resolution is lossy
// and the original class is not otherwise recoverable. Mirrors the existing
// "sacm.import.name" and "sacm.import.assertionDeclaration" conventions.
inline constexpr std::string_view kImportExtensionTypeKey = "sacm.import.extensionType";

// True when the URI belongs to a metamodel known to specialize SACM.
bool is_sacm_extension_namespace(std::string_view namespace_uri);

// True when `name` is an attribute the SACM 2.3 serialization can legitimately
// carry: an attribute or association end from the normative model, an XMI
// infrastructure attribute, or a known machine-readable-model misspelling.
//
// Losslessness gate: an unprefixed attribute outside this set is something the
// reader would otherwise ignore in silence. Preserving and reporting it turns
// silent data loss into a diagnostic. The check is name-based, not per-class,
// so an attribute that is valid SACM but wrong for its element still passes --
// that is a validation concern, not a loss one.
bool is_known_sacm_attribute(std::string_view name);

// Every attribute and association-end name the check accepts, for the drift
// test that holds it against the normative inventory.
std::span<const std::string_view> known_sacm_attributes();

// Kind classification against the abstract containment-end types.
bool kind_is_argumentation_element(ElementKind kind);
bool kind_is_artifact_element_in_artifact_package(ElementKind kind);
bool kind_is_terminology_element(ElementKind kind);
bool kind_is_artifact_asset(ElementKind kind);
bool kind_is_argument_asset(ElementKind kind);

}  // namespace sacm::io::detail
