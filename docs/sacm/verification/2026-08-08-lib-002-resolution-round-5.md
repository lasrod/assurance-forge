---
slice: lib-002-resolution
date: 2026-08-08
verdict: PASS
requirements: [SACM23-LIB-002]
commit: 7e178ac
verifier: sacm-conformance-verifier
---

## Verification result

PASS — SACM23-LIB-002 may be marked `verified` under #295 outcome 2: library
interchange conformance and application editing coverage are separate claims;
an unsupported application edit refuses visibly with the document unchanged,
and refused paths are application limitations, not unclaimed library
capability. All three round-4 exit conditions are met; per that record's
commitment, the new findings below are follow-ups, not flip-blockers.

- **Condition 1 met.** All three seam-unsupported fallbacks (`CreateTopGoal`,
  `CreateChildElement`, `CreateChallenge`) route through
  `ApplyLibraryPrimaryOrLegacy` → the guarded bridge
  (`src/core/commands/element_commands.cpp`). Pinned by
  `SaveFromLibrary.SACM23_LIB_002_TopGoalFallbackRefusesRatherThanDeleteArtifactContent`,
  which asserts the round-4 triple: the guard's refusal (the seam-rejection
  path says "The SACM library rejected", so the pin cannot pass on the wrong
  failure), tracked-file byte-identity, and live-document presence of
  `event_release`, `prop_confidentiality`, `group_evidence`. The round-4 probe
  was independently relinked against the rebuilt Release libraries at HEAD and
  re-run: probe A on `artifact-full-valid.sacm.xmi` now measures
  `success=false`, guard refusal, byte-identical file, all seven markers alive
  in file and live document — versus round 4's measured success and
  8-of-9-elements deletion at the same point. `apply_add_top_goal` verifiably
  reports unsupported on the argument-less shape, so the pin exercises the
  fallback, not the seam.
- **Condition 2 met.** The bare-reasoning safe-fail is pinned
  (`SaveFromLibrary.SACM23_LIB_002_ChildUnderReasoningFallbackFailsWithFileUntouched`;
  the fixture is genuinely bare — no inference references `AR_bare` — and
  fully representable, so the guard passes and the mutator's failure surfaces
  through the bridge). `CreateChallenge` is structurally guarded, stronger
  than the written-argument alternative the condition allowed. For
  `RemoveRelationship`, the written argument, both legs verified: (1)
  `apply_delete_element` sets `outcome.supported = true` unconditionally
  (`src/sacm_adapter/document_edit.cpp`), so the seam path always engages in
  production; (2) the raw fallbacks — including the identical shape in
  `RemoveElement` NodeAndDescendants — are reachable with a document present
  only via `allow_library_primary = false`, which nothing under `src/`
  assigns; it appears only in three test files as a deliberate test seam.
  Retiring or documenting that seam remains a standing follow-up.
- **Condition 3 met.** Matrix cell reads Activity (12.8), Term (10.7); the
  preservation record's five read 12.9/12.7/12.8/10.7/10.8 — all matching
  `libs/sacm/include/sacm/model/artifact.h` and `terminology.h`.

## Scope

- Requirement IDs: SACM23-LIB-002 (primary); AF-STD-011 re-checked — the
  round-4 overstatement caveat dissolves, the row's refusal claim is now
  universally true.
- Files inspected: `element_commands.cpp` (full), `library_bridge.cpp` (full),
  `command_bus.cpp` (diff + Stage-5 warning wording), `document_edit.cpp`
  (`apply_add_top_goal`, `apply_delete_element`), `command_bus.h`; matrix row
  diff, preservation-record diff, metamodel inventory, round-4 record,
  feature-matrix AF-STD-011. `libs/sacm` unchanged this round (diff
  0429834..7e178ac confirmed).
- Tests run (after rebuild; the committed binary predated HEAD by 44 s):
  `SaveFromLibrary.*` 22/22; the round-4 broader filter 64/64; full
  `tests.exe` 1116/1118 (2 × `DraftExampleProject.*`, environmental — locally
  modified `examples` submodule, out of scope per rounds 3–4);
  `sacm_tests.exe` 110 pass / 2 skipped (opt-in corpus); matrix, catalog and
  feature-matrix checkers clean.
- Break-verification of the condition-1 pin accepted on the lead's report,
  mechanical grounds, and the independent probe at the production measurement
  point.

## Findings

| Severity | Requirement ID | Finding | Required fix |
|---|---|---|---|
| Record-required | SACM23-LIB-002 | The `RemoveRelationship` written argument (above) satisfies condition 2's third alternative and lives in this record, extended to the identical raw-fallback shape in `RemoveElement` NodeAndDescendants. | Recorded here; none further. |
| Minor (follow-up, not a flip-blocker) | SACM23-LIB-002 | The guard's refusal message ends "creating, deleting and challenging elements still work, as they do not use it" — false in exactly the context where a create fallback was just refused (measured in probe A output). | Drop or condition the trailing sentence; no test change required. |
| Env | — | `DraftExampleProject.*` ×2 — locally modified `examples` submodule (user WIP). `LocalizationTest.*` ×2 fail only when `tests.exe` runs from `build/Release` (cwd assumption) — harness artifact. | Out of scope. |

## Matrix updates allowed

- **May mark verified: SACM23-LIB-002**, with the Notes lead sentence replaced
  by: with a library document present, every bus command either applies
  through the library seams or routes its legacy fallback through the guarded
  bridge; the seam-unsupported create fallbacks (round-4 probe A) refuse
  rather than rewrite the file. The rest of the cell — gap disclosure,
  refused-kind list with corrected clause numbers, links — stays as written.
- Must remain open: nothing on this row.

## Follow-up slice suggestions

- Fix the guard refusal message's trailing sentence (Minor above).
- Carried: retire or document `allow_library_primary`; the no-bus dispatch
  branch (`sync_library_document` unguarded, reachable for any
  standalone-opened file, typed-content consequence undisclosed); empty the
  sweep's rejected-fixtures list; extend the sweep to attribute fingerprints;
  assert the empty root-level `AP_detail` package survives the
  metaClaim/structure save; one sentence in the audit docs on replay of
  pre-fix NodeOnly events against unrepresentable documents.
