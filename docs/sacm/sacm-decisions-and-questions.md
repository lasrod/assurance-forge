# SACM library decisions and remaining questions

This document records the current project decisions for the SACM 2.3 library-first implementation. These decisions are intentionally explicit so agents do not re-open settled points or accidentally design the library around Assurance Forge internals.

## Settled decisions

| # | Topic | Decision |
|---|---|---|
| 1 | Initial location | Start inside the Assurance Forge repository, preferably under `libs/sacm`, while keeping the library independent enough to split later. |
| 2 | Implementation scope | Complete SACM 2.3 compliance is the goal. Work in smaller verified increments to keep the direction correct. |
| 3 | Package mapping | File/project root maps to `AssuranceCasePackage`; argument module/folder maps to `ArgumentPackage`; GSN goal maps to SACM `Claim`. |
| 4 | Core terminology | The reusable library uses strict SACM terminology only. Assurance Forge may use Goal, Strategy, Solution, canvas, tree, and similar UI/GSN terminology in its adapter and UI. |
| 5 | Goal API | No `Goal` terminology in the core SACM library API. Use `Claim`. GSN terminology belongs outside the library. |
| 6 | Deletion behavior | Needs deeper investigation, but destructive deletes should be previewable. The library should identify affected elements before deletion so a client can ask a human user to confirm. |
| 7 | Operation preview | Yes. The library should support previews for deletion and other operations with potentially broad consequences. |
| 8 | Validity after public edits | Yes. Public mutation operations should either succeed and leave the document SACM-valid for the supported slice, or fail and leave the document unchanged. |
| 9 | Unsupported SACM content | Valid SACM content must not be silently dropped. If not fully supported, preserve it where possible or reject/block operations that would corrupt it. |
| 10 | Strict vs compatibility | Provide strict mode and compatibility mode. Strict mode is SACM 2.3 conformance behavior; compatibility mode can preserve or accept third-party/legacy quirks when explicitly requested. |
| 11 | Round-trip guarantee | Semantic round-trip is required. Exact textual round-trip is not the baseline. Deterministic export should be tested. |
| 12 | IDs | Support both caller-provided IDs and library-generated IDs. Generated IDs must be stable after creation and deterministic enough for testing where required. |
| 13 | Source of truth | The SACM library document is the canonical source of truth after load/import. Assurance Forge state is a projection/cache/client. |
| 14 | Mutation mechanism | Assurance Forge should mutate SACM through library commands or explicit mutation APIs, not by directly editing internal data structures. |
| 15 | Undo/redo | Undo/redo is required and must align with Assurance Forge audit capabilities. Exact ownership and mechanism need more investigation. |
| 16 | Validation timing | Use cheap structural validation after every mutation and full conformance validation on save or explicit validate. |
| 17 | Standard version | Support SACM 2.3 only for now. Design should not make future version support impossible. |
| 18 | Save mode | Strict SACM 2.3 should be the default save mode. Compatibility/strict choice can be made during import and then retained for the document/session. |
| 19 | Layout | To be decided later for Assurance Forge. Representation/layout is explicitly not part of the SACM library. Assurance Forge may compute deterministic layouts from SACM data. |
| 20 | Interoperability targets | Start with Papyrus-style SACM material and possibly OASC. Let the interoperability agent research this further. |
| 21 | Compliance claim | The goal is SACM 2.3 compliance. Work continues until compliance is achieved or a real blocker is discovered. Temporary milestone claims may describe covered subsets, but the project goal remains full compliance. |
| 22 | Conformance matrix | Yes. Maintain a matrix across elements, properties, XMI behavior, editability, validation, tests, and interoperability evidence. |
| 23 | Partial implementation | Not a high-priority design area. Choose the easiest safe approach: preserve unsupported valid content where practical; otherwise reject or block operations that would risk silent loss. |
| 24 | Diagnostics | Yes. Diagnostics must be machine-readable with stable codes, severity, requirement IDs, and locations where practical. |
| 25 | External API | Start with a clean C++23 API plus CLI test utility. Do not add other bindings until the model stabilizes. |

## Non-negotiable boundary

The SACM library owns:

```text
SACM data
SACM identity and references
SACM validation
SACM edit/mutation semantics
SACM 2.3 XMI import/export
semantic round-trip behavior
strict and compatibility modes
machine-readable diagnostics
```

Assurance Forge owns:

```text
GSN terminology used in the UI
canvas and tree projections
deterministic layout
user prompts and confirmation dialogs
review workflows
project UX
visualization-specific state
```

The SACM library must never expose Assurance Forge UI concepts such as canvas, node positions, tree items, ImGui state, or GSN display names as core model concepts.

## Remaining questions for later investigation

### Delete policy details

The broad direction is preview-first destructive editing. Still decide:

- Should the default direct delete be `RejectIfReferenced`, `RequirePreviewToken`, or `CascadeOnlyWhenExplicit`?
- Should package delete support both `RejectIfNonEmpty` and `DeleteRecursively`?
- How should cross-package references be reported and handled?
- Should a preview become invalid if the document changes before the user confirms?

Recommended initial answer: implement operation preview and require explicit delete policy for destructive operations. Do not silently cascade.

### Undo/redo and audit

Undo/redo is required, and Assurance Forge audit capabilities matter. Still decide:

- Should the library expose reversible commands?
- Should the library return operation deltas that Assurance Forge stores in its audit log?
- Should undo use snapshots for early versions and deltas later?
- Which operation metadata is required for audit: actor, timestamp, command name, affected IDs, before/after values, diagnostics?

Recommended initial answer: mutation results should include enough machine-readable affected-element data to support later audit and undo/redo, even if full reversible commands are not implemented in the first slice.

### Layout and representation

Layout is outside the SACM library. Still decide in Assurance Forge:

- Should deterministic layout always be recomputed from SACM data?
- Should layout ever be cached in a separate non-SACM sidecar?
- Should external layout metadata be ignored, preserved separately, or visualized only in compatibility mode?

Recommended initial answer: compute deterministic layout from SACM data and keep strict SACM files free of layout metadata.

### Interoperability corpus

The current preference is Papyrus-style SACM material and possibly OASC. The interop researcher should determine:

- Which sample files are legally usable in the repository.
- Which files must be represented as minimized reproductions.
- Which tools produce SACM 2.3 XMI versus adjacent GSN/assurance-case formats.
- Which deviations require compatibility mode.

## Agent instruction

Agents must treat the table above as project policy unless a human maintainer explicitly changes it. When a decision remains open, choose the smallest safe implementation that preserves SACM data and avoids Assurance Forge-specific leakage.
