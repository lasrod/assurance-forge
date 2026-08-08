---
slice: lib-002-resolution
date: 2026-08-08
verdict: FAIL
requirements: [SACM23-LIB-002]
commit: 0429834
verifier: sacm-conformance-verifier
---

## Verification result

FAIL — SACM23-LIB-002 stays at `implemented`. Both round-3 blockers are
genuinely closed, correctly implemented, and pinned exactly as that record
demanded. But round-3 condition 1 read "Live NodeOnly **(and any bus command
mutating the legacy package with a library document present)** refuses or
preserves", and the hunt that parenthetical mandates found one remaining bus
command that does neither — measured with a probe against the Release static
libraries, the same method as round 3.

**Probe (A): `CreateTopGoal` on a library-loaded document with no
`ArgumentPackage`.** `apply_add_top_goal` returns `supported == false`
(`src/sacm_adapter/document_edit.cpp`), the command falls through to the raw
legacy mutator (`src/core/commands/element_commands.cpp`, `InstallTopGoal`
succeeds with no argument package), and the bus's Stage-5 sink writes
projection bytes over the tracked file. Measured on
`libs/sacm/tests/data/sacm23/artifact-full-valid.sacm.xmi` — a conforming
interchange document of exactly the shape SACM23-CP-003 certifies: command
reports success; the only banner is "Library XMI save failed; wrote the legacy
serialization (audit remains consistent)" — reassuring during the event it
should flag; the tracked file is rewritten in the legacy dialect (including a
malformed `xmlns:=""` and a duplicated id) with **8 of 9 clause-12 elements
deleted from disk**; the live document keeps them but `has_unsaved_changes` is
cleared, so closing the app makes the loss permanent. Reachable as the first
button a user would press on such a file (`AppRuntime::AddTopGoal`).

Mitigating scope, stated: it fires only on argument-less documents; probe (B)
showed `CreateChildElement`'s unsupported fallback failing visibly with the
file byte-identical, `apply_delete_element` always reports `supported = true`
so `RemoveRelationship` cannot fall through, and `CreateChallenge`'s
unsupported case has no apparent UI reachability — but none of those
safety-by-accident invariants is pinned.

## Scope

- Requirement IDs: SACM23-LIB-002 (primary); SACM23-INT-001 read, unaffected;
  AF-STD-011 re-checked per round-3 condition 5.
- Files inspected: `element_commands.cpp`, `library_bridge.cpp`,
  `command_bus.cpp/.h`, `undo_command.cpp/.h`, `sacm_argument_sync.cpp`,
  `element_factory.cpp` (InstallTopGoal), `app_state.cpp`,
  `history_reconstruction.cpp`, `src/sacm/*`, `document_edit.cpp`,
  `case_projection.cpp`, `xml_parser.cpp`, `dispatch.cpp`,
  `app_runtime_undo.cpp`, `element_edit_controller.cpp`; fixtures
  `artifact-full-valid.sacm.xmi`, `argumentation-full-valid.sacm.xmi`;
  `libs/sacm/include/sacm/model/artifact.h`, `terminology.h`, the metamodel
  inventory.
- Tests run (after a rebuild — two files at HEAD were newer than the binary):
  targeted filter 84/84 pass; full `tests.exe` 1114/1116 (2 ×
  `DraftExampleProject.*`, environmental: the `examples` submodule locally
  carries a modified `projects/kitchen-blender/arguments/main.sacm` — user
  work in progress, out of scope for this row); `sacm_tests.exe` 110 pass /
  2 skipped (opt-in corpus); matrix and feature-matrix checkers clean. Two
  probe measurements (A: CreateTopGoal fallback; B: CreateChildElement
  fallback).

## Findings

| Severity | Requirement ID | Finding | Required fix |
|---|---|---|---|
| Blocking | SACM23-LIB-002 | Probe (A): `CreateTopGoal` fallback with `ctx.library_document != nullptr` mutates legacy and the bus writes lossy projection bytes; success reported, 8 of 9 clause-12 elements deleted from disk, `has_unsaved_changes` cleared. Round-3 condition 1's parenthetical, violated verbatim. | Route the `!applied_to_library` fallback through `ApplyLibraryPrimaryOrLegacy` (the guard refuses this document by construction) or refuse outright when a document is present; pin over `artifact-full-valid.sacm.xmi` asserting refusal-or-preserve + tracked-file byte-identity + live-document content (`event_release`, `prop_confidentiality`, `group_evidence`). |
| Major | SACM23-LIB-002 | The remaining native fallbacks are safe only by accident: `CreateChildElement`'s legacy path happens to fail on the unsupported shape, `apply_delete_element` happens to always report supported. Neither invariant is pinned; a change silently reopens the probe-A class. | Pin the bare-reasoning safe-fail (visible failure + byte-identity); guard, pin, or record a written unreachability argument for `RemoveRelationship`/`CreateChallenge`. |
| Minor | SACM23-LIB-002 | Clause citations: the matrix cell's `Activity (12.11)` and `Term (10.11)` should be **12.8** and **10.7** (12.11 is Technique) per the library's own headers and the metamodel inventory; the preservation record's closing paragraph carries five wrong (Event 12.4→12.9, Artifact 12.2→12.7, Activity 12.3→12.8, Term 10.2→10.7, Category 10.5→10.8). | Correct both at flip time; treat `libs/sacm/include/sacm/model/*.h` + the inventory as the citation source. |
| Minor | SACM23-LIB-002 | The no-bus dispatch branch is reachable for any SACM file opened outside a project, not only on bus-init failure as round 3 stated; `sync_library_document` rebuilds unguarded and its typed-content consequence is undisclosed. | Standing minor per round-3 precedent (not a flip condition): correct the reachability sentence and cover typed content, or pass the document + guard into the no-bus context. |
| Minor | AF-STD-011 | Row name reads as universal while the probe-A path loses without refusing. | Notes caveat until the Blocking lands. |
| Env | — | `DraftExampleProject.*` ×2 — locally modified `examples` submodule (user WIP), not a HEAD regression. | Out of scope for this row. |

Round-3 conditions 2–5 are met: metaClaim/structure preserved and pinned on
saved bytes end-to-end; the empty nested package refuses with byte-identity;
the NodeOnly pin asserts refusal + byte-identity + live-document content, and
the flip-parity test was strengthened to require the bridge routing while
still asserting reparent semantics; the rewritten matrix cell and reconciled
preservation record are accurate as scoped (modulo the clause numbers); the
AF-STD-011 citations are corrected.

## Matrix updates allowed

- May mark verified: nothing. SACM23-LIB-002 stays `implemented`.
- Must remain open: SACM23-LIB-002. **Round-5 pass conditions — the complete
  list; nothing further will be added if these are met:**
  1. The `CreateTopGoal` fallback with a library document present refuses or
     preserves, pinned over `artifact-full-valid.sacm.xmi` as in the Blocking
     row.
  2. `CreateChildElement`'s safe-fail pinned on the bare-reasoning shape;
     `RemoveRelationship`/`CreateChallenge` guarded, pinned, or covered by a
     written unreachability argument in the round-5 record.
  3. Matrix-cell clause citations corrected (Activity 12.8, Term 10.7); the
     preservation record's five corrected likewise.
- Evidence sound and citable now: all three round-4 tests, the NodeOnly
  live/replay agreement, the metaClaim/structure plumbing, the
  document-inventory guard including the root-package id.

## Follow-up slice suggestions

- Carried from round 3: empty the sweep's rejected-fixtures list; extend the
  sweep to attribute fingerprints; retire or document `allow_library_primary`.
- The Stage-5 sink's soft warning ("audit remains consistent") should say what
  was NOT written when the XMI path fails — it reassures during the exact
  event it should be flagging.
- Replay of a pre-fix NodeOnly event recorded against an unrepresentable
  document will now refuse where the live session once applied it — worth one
  sentence in the audit docs.
- The metaClaim/structure test could additionally assert the empty root-level
  `AP_detail` package survives the save; currently only its reference is
  asserted.
