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
| 26 | SACM23-LIB-002 resolution | #295 outcome 2: library interchange conformance and application editing coverage are separate claims. An application edit the library-primary path cannot represent must refuse visibly and leave the document unchanged — never silently degrade it — and refused or guarded paths are disclosed application limitations (capability matrix), not unclaimed library capability. Verified in round 5 after two FAIL rounds; see the verification records. |
| 27 | Binding participant typing (clauses 9.4, 10.5/10.6, 11.5) | **A package binding is not a legal participant of a binding — warning, not error.** The three parallel clauses give three different rules for one concept: 9.4's OCL admits only `oclIsTypeOf(Package)` or `oclIsTypeOf(PackageInterface)` (which excludes a binding), 10.5/10.6's uses `oclIsKindOf(Package)` (which admits one), and 11.5's names the Interface subtype alone while its own Associations block declares the general package type. What all three agree on is that participants are *the packages being bound*; nothing anywhere gives a binding-of-bindings a meaning. So the library diagnoses it uniformly across all four families, at warning severity because one clause's own OCL permits it. The narrower 11.5 reading — participants must be interfaces — is **not** enforced: it contradicts its own clause and the other two. ([#333](https://github.com/lasrod/assurance-forge/issues/333)) |
| 28 | Interface/binding content citations (clauses 9.3, 11.5, 11.6, 12.4, 12.5) | **Enforced as errors; the locality half is split.** That an interface's or binding's contents must all be citations is stated as a requirement in every clause that states it at all ("only allowed with isCitation=true", "must be … citations to"), and 11.5 backs it with OCL, so it is an error. The *locality* half — that the citation points into the implemented package (interface) or a participant package (binding) — is enforced only for interfaces, at warning severity: an interface `implements` exactly one package, so the containment test has one answer. For a binding it is not enforced at all, because a participant may be named as an interface that itself resides inside the package the citation targets (clause 11.6), so the same document has two legitimate readings. Approximating that would produce warnings on conformant files. ([#333](https://github.com/lasrod/assurance-forge/issues/333)) |
| 29 | ArgumentPackage content homogeneity (clause 11.4) | **Enforced as an error, but a contained interface or binding does not trigger it.** Clause 11.4 says a package that nests ArgumentPackages "is only allowed to contain ArgumentPackages". Read literally that is unsatisfiable alongside clause 11.6, which requires an ArgumentPackageInterface to reside *inside* the package it describes — every non-empty package that declares an interface would be non-conformant by construction. The library therefore triggers 11.4 only on a nested *plain* ArgumentPackage. ([#334](https://github.com/lasrod/assurance-forge/issues/334)) |
| 30 | Severity follows the clause's own modal verb | Where the specification says "must", or gives an OCL invariant, a violation is an **error**; where it says "should" with no OCL, a **warning**. This is why clause 8.2's abstractForm rules split three ways (the citing element's `isAbstract` is stated flatly → error; the referred element's `isAbstract` and its type are "should" → warning), and why clause 8.4's expression/content exclusivity is a warning while clause 10.10's OCL is an error. Flattening them all to error would report conformant-but-unidiomatic files as broken; flattening to warning would lose the distinction the specification itself draws. ([#335](https://github.com/lasrod/assurance-forge/issues/335)) |
| 31 | Where the new clause checks live | The clause checks added for [#333](https://github.com/lasrod/assurance-forge/issues/333)/[#334](https://github.com/lasrod/assurance-forge/issues/334)/[#335](https://github.com/lasrod/assurance-forge/issues/335) are in `validate()`, **not** `validate_structure()`. `validate_structure` is asserted after every successful command in debug builds — it is the invariant every mutation must preserve — while these are constraints on document *content*, which a document can legitimately arrive violating. Putting them there would turn "this loaded file is non-conformant" into "this build aborts". The command layer keeps its existing generic ArgumentAsset end typing; it does not enforce the family-specific rules, so a client can still build a document validation will then flag. |

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
