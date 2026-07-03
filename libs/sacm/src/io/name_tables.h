#pragma once

// Internal class-name and kind-classification tables (not installed).
// Seeded from the concrete-class tables in
// docs/sacm/sacm-2.3-metamodel-inventory.md and asserted against it by
// test_metamodel_coverage.cpp.

#include "sacm/metadata/element_kind.h"

#include <optional>
#include <string_view>

namespace sacm::io::detail {

using metadata::ElementKind;

// Exact SACM 2.3 class-name lookup ("Claim" -> Claim). Also accepts the
// known ptc/22-03-13 alias ArtifactAssertedRelationship.
std::optional<ElementKind> kind_from_class_name(std::string_view name);

// Case-insensitive lookup for tolerant mode ("claim", "CLAIM").
std::optional<ElementKind> kind_from_class_name_ci(std::string_view name);

// Kind classification against the abstract containment-end types.
bool kind_is_argumentation_element(ElementKind kind);
bool kind_is_artifact_element_in_artifact_package(ElementKind kind);
bool kind_is_terminology_element(ElementKind kind);
bool kind_is_artifact_asset(ElementKind kind);
bool kind_is_argument_asset(ElementKind kind);

}  // namespace sacm::io::detail
