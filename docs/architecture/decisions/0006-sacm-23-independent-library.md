# ADR 0006: SACM 2.3 as an independent reusable library

## Status

Accepted

## Context

Assurance Forge is intended to consume and produce SACM 2.3 XML/XMI, but long-term interoperability requires a reusable SACM library rather than an application-specific data model. Other tools should be able to use the library without depending on Assurance Forge UI, app runtime, project files, AI review behavior, deterministic layout behavior, or current parser/view models.

SACM compliance must be measured at the XMI/model/editing boundary, not at the UI boundary. Assurance Forge can visualize and edit through GSN terminology, but the loaded SACM document must preserve standard data that the UI does not yet display.

## Decision

Implement SACM 2.3 as an independent C++23 library inside the Assurance Forge repository, preferably under `libs/sacm`, while keeping it independent enough to split later.

The library owns:

- SACM model state.
- SACM-native editing and mutation semantics.
- Operation previews for destructive edits.
- XMI import/export.
- Identity and reference resolution.
- Validation and diagnostics.
- Strict and compatibility modes.
- Conformance-related metadata and tests.

Assurance Forge will use the library through adapter/projection code. Derived UI models and deterministic layouts must not become the source of truth for SACM serialization.

## Terminology decision

The core library uses SACM terminology only. Examples:

```text
AssuranceCasePackage
ArgumentPackage
Claim
AssertedInference
AssertedEvidence
ArtifactReference
TerminologyPackage
```

Assurance Forge may expose GSN/UI terms such as:

```text
Goal
Strategy
Solution
Canvas
Tree
```

Those terms belong in Assurance Forge or an optional adapter, not the SACM library.

## Editing decision

Editing is part of the first real vertical slice. The first slice should create and delete minimal SACM package/claim structures, validate them, save them, reload them, and prove semantic round-trip.

Public mutations should be atomic:

```text
success -> changed and valid for the supported slice
failure -> unchanged with diagnostics
```

Destructive operations should provide operation previews so clients can show affected elements before applying the mutation.

## Layout decision

Layout is not part of the SACM library. Strict SACM 2.3 export must not include Assurance Forge layout metadata. Assurance Forge may compute deterministic layout from SACM data.

## Consequences

Positive:

- The SACM implementation can be reused by other tools.
- Compliance claims can be tied to library tests and a conformance matrix.
- Assurance Forge can evolve UI workflows without redefining SACM semantics.
- Hidden standard data can survive load/project/edit/save workflows.
- Delete confirmation can be based on library-provided affected-element data.
- The library can eventually be split into its own package or repository.

Negative:

- Current Assurance Forge parser/domain structures may need migration or replacement.
- Adapter code is required to bridge SACM data into existing views.
- Delete preview, validity-preserving edits, and audit/undo data require careful API design.
- Full SACM 2.3 coverage requires substantial matrix and fixture work.

## Implementation requirements

- Public library headers must not include Assurance Forge app, UI, core, parser, AI, review, or project headers.
- CMake target names must be neutral, such as `sacm` and `sacm::sacm`.
- Strict SACM 2.3 import/export behavior must be separated from legacy compatibility modes.
- Standard SACM data must not be silently discarded.
- Strict SACM output must not include Assurance Forge layout metadata.
- Every feature slice must follow: requirements -> tests -> implementation -> verification.
- The first slice must include editing, validation, save/load, and semantic round-trip.

## Verification

This ADR is verified by:

- A standalone SACM library target that builds and tests without Assurance Forge app/UI dependencies.
- Conformance matrix coverage.
- SACM 2.3 XMI fixtures.
- Create/delete command tests.
- Operation preview tests.
- Import/export/round-trip tests.
- CLI smoke tests.
- Adapter tests proving Assurance Forge saves from library state and computes layout outside the library.
- Include/dependency checks preventing library API leaks.
