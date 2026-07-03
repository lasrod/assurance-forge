#pragma once

#include "sacm/commands/policies.h"
#include "sacm/model/element_id.h"
#include "sacm/model/lang_string.h"

#include <optional>
#include <string>
#include <variant>

namespace sacm::commands {

// SACM-native edit operations (aggregate structs; designated initializers
// encouraged). Omitted `id` fields are generated deterministically by the
// document. The Operation variant grows slice by slice.

// Create an AssuranceCasePackage: a document root when `parent` is empty,
// or a nested package inside another AssuranceCasePackage.
struct CreateAssuranceCasePackage {
    std::optional<model::ElementId> id;
    std::string name;
    std::optional<model::ElementId> parent;
};

// Create an ArgumentPackage inside an AssuranceCasePackage or (nested)
// inside another ArgumentPackage.
struct CreateArgumentPackage {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
};

// Create a Claim inside an ArgumentPackage. `description` becomes the
// Claim's Description content (SACM stores claim text there).
struct CreateClaim {
    model::ElementId parent;
    std::optional<model::ElementId> id;
    std::string name;
    std::string description;
    std::string language;  // language tag for name/description; may be empty
};

// Set or clear an element's citation (clause 8.2): `cited` present sets
// citedElement and isCitation=true; absent clears both.
struct SetCitation {
    model::ElementId element;
    std::optional<model::ElementId> cited;
};

// Delete any element by ID. Destructive consequences are governed by the
// explicit policies; defaults reject rather than cascade.
struct DeleteElement {
    model::ElementId target;
    ReferenceDeletePolicy reference_policy = ReferenceDeletePolicy::RejectIfReferenced;
    PackageDeletePolicy package_policy = PackageDeletePolicy::RejectIfNonEmpty;
    CrossPackageReferencePolicy cross_package_policy =
        CrossPackageReferencePolicy::RejectIfExternalReferencesExist;
};

using Operation = std::variant<CreateAssuranceCasePackage, CreateArgumentPackage, CreateClaim,
                               SetCitation, DeleteElement>;

// Stable operation name ("CreateClaim", "DeleteElement", ...) used in
// previews, results, and diagnostics.
std::string_view operation_name(const Operation& operation);

}  // namespace sacm::commands
