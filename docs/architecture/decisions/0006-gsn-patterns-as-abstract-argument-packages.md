# 0006. GSN patterns persisted as abstract ArgumentPackages

- Status: Proposed
- Date: 2026-06-18
- Deciders: Assurance Forge maintainers

## Context

Assurance Forge is adding a GSN Pattern Editor (GSN Community Standard v3,
Argument Pattern Extension). A pattern is a reusable argument template that
carries GSN-specific notation with no direct SACM equivalent: an
*uninstantiated* element decorator, *optional* and *multiplicity* relationship
operators with cardinality, and *choice* groups, plus a structured pattern
*definition* (intent, applicability, participants, …).

SACM 2.3 does not define a pattern, template, or `PatternPackage` metaclass. It
does, however, provide general extension mechanisms on every element:
`isAbstract`, `abstractForm`, and `TaggedValue` (clause 8.10). [ADR-0003](0003-sacm-xml-as-source-of-truth.md)
commits us to SACM XML as the canonical representation with full round-trip
integrity, so any pattern state we introduce must survive parse → serialize →
parse and must not require a non-standard SACM structure.

We also need the project explorer and the editor to reliably tell a pattern
apart from an ordinary argument, and we must keep a clean separation between
"this entity is abstract because it belongs to a reusable pattern" and "this
element visibly carries the GSN uninstantiated decorator" — every pattern entity
is the former, but only selected elements are the latter.

## Decision

We will represent each GSN pattern as an ordinary **abstract SACM
`ArgumentPackage`**, not a new SACM class, and carry all GSN-specific pattern
notation as namespaced SACM `TaggedValue` entries.

Concretely:

- A pattern package has `isAbstract = true` and a classifying tagged value
  `assuranceforge.view.kind = gsn-pattern`. A package is treated as a pattern
  only when **both** hold; it is never inferred from the package name or its
  location in the project tree.
- All pattern-owned elements and relationships are normal SACM argument assets
  with `isAbstract = true`, retaining their normal GSN/SACM semantic typing.
  Optionality, multiplicity, and choice are metadata on ordinary `SupportedBy`
  / `InContextOf` relationships — they never replace the relationship type.
- The visible uninstantiated decorator is a separate tagged value
  (`assuranceforge.gsn.pattern.uninstantiated = true`), independent of
  `isAbstract`.
- Cardinality is stored as **structured** lower/upper bound tokens (an integer,
  a parameter name, or `unbounded`) plus a preserved display expression, so
  later instantiation never has to re-parse free-form text.
- A choice group is a first-class application concept reconstructed from member
  relationships' tagged values; it is **never** serialized as a SACM Claim,
  ArgumentReasoning, ArtifactReference, or any other element.
- All pattern tagged-value keys live in a single header
  (`src/sacm/pattern_keys.h`); the UI-independent read/write helpers live in
  `core` (`src/core/pattern_model.{h,cpp}`). The literal key strings are not
  duplicated elsewhere.

## Consequences

- Patterns live in the same `.sacm` file as ordinary arguments, the exported
  SACM stays structurally valid, and existing files without patterns are
  unaffected — they simply carry no pattern tagged values.
- Pattern entities are abstract, so a later plan can point concrete instantiated
  elements at their source via `abstractForm` for provenance.
- We accept that GSN pattern semantics are expressed through Assurance Forge
  conventions layered on SACM extension points rather than native SACM features;
  a different SACM tool will see valid abstract packages with opaque tagged
  values, not patterns.
- Round-trip tests must assert `gid`, `isAbstract`, `abstractForm`, and tagged
  values (previously unchecked), since pattern fidelity depends on them.
- The `isAbstract` vs. visible-uninstantiated distinction must be respected in
  both directions: marking an element uninstantiated must not be derived from
  `isAbstract`, and clearing it must not clear `isAbstract`.
