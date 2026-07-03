#pragma once

namespace sacm::commands {

// Policies for destructive operations. Defaults are always the reject
// variants: cascading effects happen only by explicit opt-in, never
// silently (editing policy, settled decision #6).

// What to do when the delete target (or a deleted descendant) is referenced
// by elements outside the deleted subtree.
enum class ReferenceDeletePolicy {
    // Fail with SACM-CMD-002 if any outside reference exists.
    RejectIfReferenced,
    // Delete referencing asserted/artifact relationships; remove other
    // referencing entries (group membership, citations, ...) from their
    // owners. No reference is ever left dangling.
    DeleteReferencingRelationships,
};

// What to do when the delete target is a package that contains elements.
enum class PackageDeletePolicy {
    // Fail with SACM-CMD-006 if the package contains model elements
    // (utility metadata such as descriptions/notes does not count).
    RejectIfNonEmpty,
    // Delete the package with all contained elements.
    DeleteRecursively,
};

// What to do when references into the deleted subtree originate from a
// different package than the target's owning package.
enum class CrossPackageReferencePolicy {
    // Fail with SACM-CMD-007 if cross-package references exist.
    RejectIfExternalReferencesExist,
    // Handle cross-package referrers under the ReferenceDeletePolicy rules.
    DeleteExternalReferencingRelationships,
};

}  // namespace sacm::commands
