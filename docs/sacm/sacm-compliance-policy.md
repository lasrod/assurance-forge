# SACM compliance policy

## Goal

The project goal is full OMG SACM 2.3 compliance for the library. Work should proceed in verified increments, but the long-term target is not merely a SACM-flavored subset.

## Compliance boundary

Compliance is judged at the SACM model/XMI/library boundary, not by the visual layout or by Assurance Forge UI behavior.

A feature is not compliant merely because it appears correctly in a GSN canvas. A feature becomes verified only when the library model, XMI import/export, validation, tests, and conformance matrix support the claim.

## Strict SACM 2.3 mode

Strict mode is the default save/export behavior and should produce clean SACM 2.3 XMI.

Strict mode must not include:

```text
Assurance Forge layout metadata
canvas coordinates
tree expansion state
Goal/Strategy/Solution UI terminology as model element names
compatibility-only vendor extensions
legacy SACM version output
```

## Compatibility mode

Compatibility mode may accept or preserve older SACM versions, vendor extensions, and third-party quirks when explicitly requested. It must not be confused with strict SACM 2.3 output.

Compatibility behavior must be documented and tested separately.

## Round-trip policy

Semantic round-trip is required:

```text
load -> save -> load
```

must preserve the SACM semantics covered by the implemented slice.

Exact textual round-trip is not required as a baseline. Deterministic export is required for repeatable tests and usable diffs.

## Unsupported content policy

Valid SACM content must not be silently dropped. The easiest acceptable approach during partial implementation is:

```text
preserve unsupported content where practical
warn that support is incomplete
block edits that would corrupt preserved content
reject load/save/edit when preservation cannot be guaranteed
```

Do not count opaque preservation as full implementation of the corresponding SACM standard element.

## Editing compliance

Public mutation operations should be SACM-native and validity-preserving.

A mutation operation should either:

```text
succeed and leave the document valid for the supported slice
```

or:

```text
fail and leave the document unchanged
```

Destructive operations must expose consequences through operation previews or equivalent structured data.

## Diagnostics

Diagnostics should be stable and machine-readable:

```text
code
severity
requirement ID
location where practical
affected element IDs
message
```

This supports tests, UI presentation, CI, audit trails, and future undo/redo design.

## Verification gate

A matrix row can move to `verified` only when:

- The relevant SACM source is identified.
- Positive tests exist.
- Negative tests exist where applicable.
- Import/export/semantic round-trip behavior is tested where relevant.
- Editing behavior is tested where relevant.
- Strict and compatibility behavior are not conflated.
- The library boundary is clean.
- The conformance verifier has reviewed the slice.
