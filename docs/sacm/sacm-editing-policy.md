# SACM editing and mutation policy

## Position

Editing belongs in the first real SACM library vertical slice. The library should not be a read-only parser first and an editor later. Assurance Forge needs creation, deletion, validation, and save behavior to use the library as its source of truth.

The editing API must still be SACM-native and independent of Assurance Forge UI representation.

## First editable vertical slice

The first implementation slice should support this end-to-end path:

```text
create new SACM document
create AssuranceCasePackage
create ArgumentPackage
create Claim
preview delete Claim
apply delete Claim
preview delete ArgumentPackage
apply delete ArgumentPackage
validate document
save strict SACM 2.3 XMI
load saved SACM 2.3 XMI
verify semantic round-trip
```

The UI term "goal" maps to SACM `Claim`, but the core library API should only expose `Claim`.

## Public mutation style

Prefer explicit command-like mutation APIs. The exact C++ form may evolve, but the behavior should be equivalent to:

```cpp
sacm::model::Document document;

auto createdCase = document.apply(sacm::commands::CreateAssuranceCasePackage{
    .id = sacm::model::ElementId::from_string("case-1"),
    .name = sacm::model::LangString::english("Brake system safety case")
});

auto createdArgumentPackage = document.apply(sacm::commands::CreateArgumentPackage{
    .parent = createdCase.created_id(),
    .id = sacm::model::ElementId::from_string("arg-pkg-1"),
    .name = sacm::model::LangString::english("Top-level argument")
});

auto createdClaim = document.apply(sacm::commands::CreateClaim{
    .argument_package = createdArgumentPackage.created_id(),
    .id = sacm::model::ElementId::from_string("claim-1"),
    .content = sacm::model::LangString::english("Brake system is acceptably safe")
});
```

Deletion should be explicit and previewable:

```cpp
auto preview = document.preview(sacm::commands::DeleteClaim{
    .claim = createdClaim.created_id(),
    .reference_policy = sacm::commands::ReferenceDeletePolicy::DeleteReferencingRelationships
});

if (preview.can_apply()) {
    auto result = document.apply(preview);
}
```

The API should not mention canvas, coordinates, tree nodes, GSN goals, strategies, solutions, or Assurance Forge command types.

## Operation preview

A preview describes what would happen before the document changes.

A preview should include:

```text
operation name
requested target ID(s)
required policy choices
elements that would be created, changed, or deleted
relationships that would be deleted or changed
cross-package effects
diagnostics
whether the operation can be applied
whether the resulting document would remain valid for the supported slice
```

Example preview text for a UI:

```text
Deleting Claim claim-17 would also delete:
- AssertedInference rel-2 because it targets claim-17
- AssertedEvidence rel-5 because it targets claim-17

The resulting document would remain valid.
```

The library returns structured data; Assurance Forge decides how to present that to the user.

## Delete policies

Initial policy enum suggestions:

```cpp
enum class ReferenceDeletePolicy {
    RejectIfReferenced,
    DeleteReferencingRelationships
};

enum class PackageDeletePolicy {
    RejectIfNonEmpty,
    DeleteRecursively
};

enum class CrossPackageReferencePolicy {
    RejectIfExternalReferencesExist,
    DeleteExternalReferencingRelationships
};
```

The exact names can change, but the semantics must stay explicit. Destructive cascades must not happen silently.

## Validity rule

Public mutation operations should be atomic:

```text
success -> document changed and remains valid for the implemented slice
failure -> document unchanged and diagnostics returned
```

Internal builders may temporarily assemble incomplete structures, but public editing APIs used by Assurance Forge and external tools should not leave the document in an invalid intermediate state.

## Diagnostics

Mutation diagnostics must be machine-readable and testable:

```text
stable code
severity
requirement ID
operation name
affected element IDs
source location where available
human-readable message
```

Example codes:

```text
SACM-CMD-001 Command target not found
SACM-CMD-002 Delete rejected because element is referenced
SACM-CMD-003 Preview expired because document changed
SACM-REF-001 Dangling reference
SACM-XMI-001 Invalid SACM 2.3 root object
```

## Undo/redo and audit alignment

Undo/redo requires more design, but the first mutation implementation should not block it. Mutation results should return enough information for an audit-capable client:

```text
operation kind
operation input
created IDs
changed IDs
deleted IDs
deleted relationships
before/after values where practical
validation result
diagnostics
```

A later design may implement reversible commands, operation deltas, or document snapshots. The first slice should at least make mutation effects explicit and stable.

## Assurance Forge integration

Assurance Forge should map UI actions to SACM commands:

```text
User creates GSN goal
  -> adapter calls CreateClaim
  -> library mutates Document
  -> adapter rebuilds deterministic UI projection

User deletes visible goal
  -> adapter calls preview(DeleteClaim)
  -> Assurance Forge shows affected elements
  -> user confirms
  -> adapter applies preview/command
  -> adapter rebuilds projection
```

The SACM library does not know how the result is displayed.
