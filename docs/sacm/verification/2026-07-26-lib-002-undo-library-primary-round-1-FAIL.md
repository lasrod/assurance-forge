---
slice: undo-library-primary
date: 2026-07-26
verdict: FAIL
requirements: [SACM23-LIB-002]
commit: 2364d370be4ffaaf49190815072339d5dfe71b30
verifier: sacm-conformance-verifier
---

## Verification result

FAIL

Not because the two slices are wrong. Both are correct, both are genuinely
pinned, and I could falsify each one independently. The row fails because
`SACM23-LIB-002` as a whole cannot honestly stand at `verified` right now:

1. its note carries the stale, self-contradicting in-memory clause that the
   INT-001 round-5 record assigned to this pass (confirmed stale — Finding 1);
2. the "undo is no longer unflipped" matrix edit was applied to one of three
   places in the same cell, so the cell now contradicts itself twice
   (Finding 2);
3. **there is a sixth instance of the projection-rebuild defect, and I measured
   it destroying preserved conforming content in the tracked working file**:
   `core::audit::MigrateStrategyEncodingIfNeeded` (Finding 3). It runs at
   project open, is silent, and is not recoverable from inside the application.
   That is the row's own requirement being violated on the working-file path,
   at a site the row already cites with a justification that is factually
   false.

Findings 1, 2 and 3 are the blocking set. Finding 3 is the only one that needs
code.

## Scope

- Requirement IDs: `SACM23-LIB-002`.
- Library files inspected (`libs/sacm`): **none changed by these slices.**
  Confirmed by `git diff b91852e~1..2364d37 --stat` — no path under
  `libs/sacm/`. `cmake/check_layer_gates.cmake` re-run at configure:
  `-- Layer-gate check passed (…/src; libs/sacm independence).`
- Application/adapter files inspected:
  - `src/core/audit/history_reconstruction.cpp` / `.h`
  - `src/core/commands/undo_command.cpp` / `.h`
  - `src/core/commands/command_bus.cpp` / `.h`
  - `src/app/app_runtime_undo.cpp`
  - `src/app/commands/dispatch.cpp`
  - `src/app/app_runtime.cpp` (`RebuildDerivedViewsIfNeeded`)
  - `src/app/areas/canvas_history_overlay.cpp`
  - `src/core/derived_views.cpp` / `.h`
  - `src/core/library_package_projection.h`
  - `src/core/argument_package_projection.cpp`
  - `src/core/app_state.cpp`
  - `src/core/audit/audit_recovery.cpp`
  - `src/core/audit/replay_verifier.cpp`
  - `src/core/audit/strategy_migration.cpp`
  - `src/app/app_runtime_project.cpp`
  - `src/sacm_adapter/library_load.h` / `.cpp`
- CLI/tooling: `tools/sacm/check_conformance_matrix.py` (exit 0, all five checks
  OK), `tools/i18n/check_catalog.py` (exit 0).
- Tests run:
  - `ctest --test-dir build -C Release` → **805/805 passed**, 2 skipped
    (`Sacm23InteropCorpus.SACM23_COMPAT_002_*`, corpus not present).
  - Targeted: `--gtest_filter='*SACM23_LIB_002*:HistoryReconstruction.*:UndoCommand.*:UndoBoundary.*:UndoResolver.*'`
    → 28/28.
  - Falsification runs (three separate builds, described below).
- Out-of-band measurement: two standalone probes compiled against the Release
  static libs, exercising paths ctest does not reach (`VerifyProject` after a
  library-primary undo; chained undo; the strategy migration). Working tree
  restored and confirmed clean at
  `2364d370be4ffaaf49190815072339d5dfe71b30`.

## Findings

| Severity | Requirement ID | Finding | Required fix |
|---|---|---|---|
| Blocking | SACM23-LIB-002 | **The assigned stale clause is confirmed stale, and it contradicts its own cell.** The note's "Remaining work" sentence reads: *"the command bus's UNFLIPPED path (NodeOnly removal, no-document dispatch) still writes projection bytes **and reloads the live document from them, so an unflipped command costs preserved vendor content on disk and in memory**"*. `command_bus.cpp:196-198` reloads via `sacm_adapter::reload_document_keeping_compatibility_content`, not the plain reload, so the in-memory half stopped being true. The same cell already says the opposite 6 000 characters earlier ("the **file** is degraded but the **document** is not … preserved vendor content survives in memory"), and cites the test that proves it. | In the LIB-002 note, replace the clause `still writes projection bytes and reloads the live document from them, so an unflipped command costs preserved vendor content on disk and in memory;` with: `still writes \`core::library_xmi_from_package\` projection bytes to the tracked file, so an unflipped command costs preserved vendor content **on disk** until the next save that serializes the document; the live document keeps it, because the Stage-5 net re-derives through \`sacm_adapter::reload_document_keeping_compatibility_content\` (SaveFromLibrary.SACM23_INT_001_UnflippedBusCommandPreservesUnknownContentInTheDocument);` |
| Blocking | SACM23-LIB-002 | **The "undo is no longer unflipped" edit was applied to one of three places.** The cell asserts "**Undo is now library-primary**, which removes it from the unflipped set named above" — but the set named above still names it, twice: (a) *"because an unflipped command — undo, a NodeOnly removal, an unsupported seam — mutated the legacy package in place"*; (b) *"whenever the library-primary flip did not engage (a NodeOnly removal, **an undo**, a dispatch with no document)"*. Only the third mention (the Remaining-work list) was updated in 2364d37. The cell now contradicts itself. | Two exact edits in the LIB-002 note: (1) `an unflipped command — undo, a NodeOnly removal, an unsupported seam —` → `an unflipped command — a NodeOnly removal, an unsupported seam —`; (2) `(a NodeOnly removal, an undo, a dispatch with no document)` → `(a NodeOnly removal, a dispatch with no document)`. |
| Blocking | SACM23-LIB-002 | **Sixth instance of the defect, measured, on the tracked working file.** `MigrateStrategyEncodingIfNeeded` (`src/core/audit/strategy_migration.cpp`) loads the tracked SACM into a library document (`:202`, `outcome.document`), projects it (`:205`), normalizes the **projection**, and writes `core::library_xmi_from_package(package)` back over the tracked file (`:212`, `:219`) plus `sacm::serialize_sacm(package)` as the promoted trusted baseline (`:244`). Measured on a legacy-encoded case carrying one foreign-namespace element: tracked file and promoted baseline both lose `xmlns:acme`, `<acme:vendorMetadata>` and `reviewCycle="Q3-2026"`; snapshot 0 keeps them. It runs at project open **before** the load (`app_runtime_project.cpp:268-282`), and the status it shows (`strategy_migration.note`) announces the migration but says nothing about the loss. Because `manifest.replay_root_snapshot_id` is repointed at the degraded baseline (`:256`), `RestoreSacmFromAudit` replays from it and cannot recover the content; and the baseline is an undo wall via `FindUndoBoundary`, so undo cannot reach snapshot 0 either. The in-code justification at `:239-241` — *"this function normalizes a projection rather than a document (there is no library document at this point in the migration)"* — is false: the document is three lines above and is discarded. This is not the same category as the accepted unflipped-autosave residual, which the in-memory document repairs on the next save; this one is silent and unrecoverable from inside the app. | Route the migration through the document it already holds: `reload_document_keeping_compatibility_content(*outcome.document, *migrated_xml)` then `sacm_adapter::save_document(*outcome.document)` for the tracked file, and write the baseline from the same document. Delete the false `no library document` justification. Pin with a test named `SACM23_LIB_002_StrategyMigrationPreservesUnknownContent` asserting on the **bytes** of both the migrated tracked file and the promoted baseline (a canonical-hash assertion cannot see this). If the fix is deferred instead, the row must drop to `implemented` — a disclosure alone is not enough for a silent, in-app-unrecoverable loss on the working-file path. |
| Minor | SACM23-LIB-002 | **No test asserts the audit chain still converges on the library-primary undo path.** Slice 2 changes which bytes an undo writes (`save_document(document)` instead of `library_xmi_from_package(projection)`), and every audit-convergence test that involves undo (`UndoCommand.EmitsUndoEventAndRestoresPriorState`, `.ReplayerSkipsUndoneTransactions`, `.TwoSuccessiveUndosWalkBackwards`) uses the **legacy** `MakeFixture` with no library document, so all three take the un-flipped branch. No test in the suite runs `VerifyProject` after an undo at all. I measured it out-of-band and it is fine — after each of three chained library-primary undos, `VerifyProject` returned `success=true cause=None` with `replayed == on_disk == manifest` — but the property is unpinned in CI. | Add `UndoCommand.SACM23_LIB_002_LibraryPrimaryUndoKeepsTheAuditChainConverged`: library-backed fixture, two edits, undo, assert `VerifyProject(...).success` and `replayed_canonical_hash == on_disk_canonical_hash == manifest_canonical_hash`. |
| Minor | SACM23-LIB-002 | **Two code comments in `command_bus.cpp` still name undo as an unflipped command**, the same drift the matrix edit was fixing: `:96` *"an unflipped command (undo, a NodeOnly removal, an unsupported seam)"* and `:194` *"on every unflipped command (NodeOnly removal, undo, an unseamed command)"*. Undo is unflipped now only when there is no library document — which the "no-document dispatch" case already covers. | Remove `undo` from both lists (or qualify it as "an undo with no library document"). |
| Info | SACM23-LIB-002 | **Slice 1 is now pinned only by the two `HistoryReconstruction.SACM23_LIB_002_*` tests.** Measured: with slice 1 reverted and slice 2 intact, `UndoCommand.SACM23_LIB_002_UndoPreservesVendorTaggedValuesInTheSavedFile` **passes** — because the flip makes the document authoritative for the saved bytes, the reconstruction's view derivation no longer reaches disk on the app path. The commit message's "all three were confirmed to fail before the fix" was true when written; it is no longer the current coupling. The two unit pins do fail (I confirmed), so slice 1 remains covered. Worth knowing before anyone "simplifies" `ReconstructAtSequence` back. | None. Optionally note the coupling in the note. |
| Info | SACM23-LIB-002 | **The legacy undo routing (`undo_command.cpp:33-36`) has no vendor-content coverage.** It is reachable with no library document or with the `allow_library_primary` kill switch off. `UndoCommand.EmitsUndoEventAndRestoresPriorState` exercises it but asserts nothing about TaggedValues. It is protected transitively (it consumes `views.package`, which the `HistoryReconstruction` pins cover), but nothing asserts the legacy path's *saved bytes*. | Follow-up. |
| Info | SACM23-LIB-002 | **`AppState::load_file` (`app_state.cpp:57-66`) is the last inline copy of `RebuildDerivedViewsFromLibrary`.** Both `CommandBus::Execute` and now `ReconstructAtSequence` call the shared helper; `load_file` still open-codes the same four steps. This is exactly the shape that turned `BridgeViaLegacy` into a second defect site — the copy drifts and the fix lands on one of them. | Have `load_file` call `core::RebuildDerivedViewsFromLibrary`. |
| Info | SACM23-LIB-002 | **Test-strength notes on the two new undo tests, neither of which changes the verdict.** (a) `…UndoPreservesVendorTaggedValuesInTheSavedFile` checks the file with substring existence (`after.find("assuranceForge.acp") != npos`), so a partial tag loss would slip past the file assertion; the live-package count assertion covers it in practice, since the package is re-derived from the saved document. (b) `…LibraryPrimaryUndoPreservesUnknownVendorContent` checks only the element name `vendorMetadata`, not the `reviewCycle` attribute or the `xmlns:acme` declaration — the class of half-preservation COMPAT-001 was about. I measured both: after the undo the file still carries the namespace declaration, the element and the attribute, and reloads cleanly, so the claim holds; the test just does not say so. | Optionally assert `Q3-2026` and `http://acme.example/toolchain` too. |

### Falsification evidence

Each fix reverted separately, rebuilt, run, then restored with `git checkout --`.

| Reverted | Test | Result |
|---|---|---|
| Slice 1 only (`RebuildDerivedViewsFromLibrary` → `project_case` + `project_library_package` in `history_reconstruction.cpp`) | `HistoryReconstruction.SACM23_LIB_002_ReconstructionPreservesVendorTaggedValues` | **FAILED** — acp tags 0 vs 5, argumentPackages 1 vs 2 |
| Slice 1 only | `HistoryReconstruction.SACM23_LIB_002_ReconstructionCarriesBareStrategyPlacement` | **FAILED** — placement absent |
| Slice 1 only | `UndoCommand.SACM23_LIB_002_UndoPreservesVendorTaggedValuesInTheSavedFile` | passed (see Info finding — the flip masks it) |
| Slice 2 only (flip guard forced false in `undo_command.cpp:18`) | `UndoCommand.SACM23_LIB_002_LibraryPrimaryUndoPreservesUnknownVendorContent` | **FAILED** — `ctx.library_primary` false; `vendorMetadata` absent from the file |
| Both | `UndoCommand.SACM23_LIB_002_UndoPreservesVendorTaggedValuesInTheSavedFile` | **FAILED** — `strategyTarget` and `assuranceForge.acp` gone from the file; live acp tags 0 vs 3 |
| Both | `UndoCommand.SACM23_LIB_002_LibraryPrimaryUndoPreservesUnknownVendorContent` | **FAILED** |

All four new tests are non-vacuous against the defect they were written for. None
of them rests on a canonical hash — correct, since the hash re-projects through
`project_library_package` on both sides and is structurally blind to exactly this
loss.

### Question-by-question

**Q2 — do the assertions demonstrate what the requirement demands?** Yes. Both
undo tests assert on the saved bytes; both carry explicit non-vacuity guards
(`…LibraryPrimaryUndo…` asserts the snapshot carries the vendor content *and*
that the edit preceding the undo did not drop it, so it cannot pass by never
having had anything to lose). `…ReconstructionPreservesVendorTaggedValues`
compares against measured live counts rather than guessed constants. The two
`Info` notes above are strength, not validity.

**Q4 — does library-primary undo break the audit chain?** No, measured. Probe:
library-backed project with a foreign-namespace element, four audited edits, then
three chained undos, `VerifyProject` after each:

```
after edits   success=true cause=None  replayed=2c5909458cec on_disk=2c5909458cec manifest=2c5909458cec
after undo 1  success=true cause=None  replayed=acb7ede8e60a on_disk=acb7ede8e60a manifest=acb7ede8e60a
after undo 2  success=true cause=None  replayed=acb7ede8e60a on_disk=acb7ede8e60a manifest=acb7ede8e60a
after undo 3  success=true cause=None  replayed=47f1935d6731 on_disk=47f1935d6731 manifest=47f1935d6731
```

Every undo reported `library_primary=true`, and the vendor element survived all
three. (Undo 2 leaving the hash unchanged is itself the hash-blindness property:
it undid `AddAcp`, whose entire effect is vendor TaggedValues, which the canonical
projection drops.) `Replayer::ReplayToLibrary`, `RestoreSacmFromAudit` and
`VerifyProject` all derive through `project_library_package` on both sides, so the
change in *which* bytes an undo writes cannot move them. Snapshot/baseline
boundaries are untouched — `FindUndoBoundary`/`CanUndo` are unchanged and still
gate `AppRuntime::Undo`. Redo composes as before: `FindUndoTarget` resolves
transitively and the routing is identical.

**Q5 — is the move-assign sound?** Yes. `LibraryDocument::operator=(LibraryDocument&&)`
is `= default` over a `unique_ptr<Impl>` (`library_load.cpp:15`), so
`*ctx.library_document = std::move(*prior_document_)` replaces the *contents* and
keeps the wrapper object. `AppState::library_document` (a `unique_ptr<LibraryDocument>`)
and `ctx.library_document` (a raw pointer to that same wrapper, from
`dispatch.cpp:83`) both stay valid. Nothing holds a pointer *into* the Impl across
a dispatch — every `LibraryDocumentAccess::{document,mutable_document}` call in
`src/sacm_adapter/` is function-local. `prior_document_.reset()` afterwards
destroys a wrapper whose `impl_` is null, which `~LibraryDocument() = default`
handles. The second-`Apply` hazard (moved-from `prior_model_`) is pre-existing and
unreachable: the replayer never reconstructs an `UndoLastTransactionCommand`, it
skips undone transactions.

Also confirmed, since 2364d37 claims it: `undo_command.cpp:34-35` is now the
**only** `ctx.model =` / `ctx.package =` in the whole command layer, and it is
reachable only without a library document. The mid-dispatch wholesale replace is
genuinely retired for the app path.

**Q7 — are the matrix edits accurate?** The added file citations
(`history_reconstruction.cpp`, `undo_command.cpp`) and the four added test names
are correct and the gate passes. The "verified status predates these changes"
claim is accurate as of `2364d37` and stops being accurate the moment this record
lands. The "removes undo from the unflipped set" edit is Finding 2.

## Matrix updates allowed

- **May mark verified:** nothing. `SACM23-LIB-002` must not stay at `verified`
  while Finding 3 stands.
- **Must remain open:** `SACM23-LIB-002`. Set it to `implemented` until the
  following four conditions are met, then it is verifiable:
  1. Finding 3 is fixed in `src/core/audit/strategy_migration.cpp` — the
     migration serializes the library document (tracked file **and** promoted
     baseline) rather than `library_xmi_from_package` / `sacm::serialize_sacm`
     of the projection — and the false `no library document` justification at
     `:239-241` is deleted.
  2. A test named `SACM23_LIB_002_StrategyMigrationPreservesUnknownContent`
     exists, asserts on the **bytes** of the migrated tracked file and of the
     promoted baseline, and is confirmed to fail before the fix.
  3. The Finding 1 clause is replaced with the exact wording given in that row.
  4. Both Finding 2 edits are applied, so no sentence in the cell still lists
     undo as an unflipped command.
  Then replace the "verified status predates these changes" sentence with a
  citation of this record plus the follow-up pass.
- **May be cited as evidence now, independent of the above:** the four new tests.
  Both slices' claims are true and falsifiable; keep them cited.
- `SACM23-INT-001` and `SACM23-INT-002` are unaffected by these slices and stay
  `verified`. Note that Finding 1 discharges the correction the INT-001 round-5
  record assigned here — that assignment is now closed.

## Follow-up slice suggestions

1. **Retire the "rebuild from a projection of myself" shape structurally, not
   site by site.** Six instances, four passes, and site 6 was found by grepping
   for `library_xmi_from_package(` rather than by anything the type system or the
   test suite said. Make the preserving reload the default and rename the lossy
   one to something a reviewer cannot pass over (`*_dropping_unknown_content`),
   so a new caller has to opt out of preservation on purpose. The INT-001 record
   already proposed this; site 6 is the argument for scheduling it.
2. **A byte-level preservation assertion as a shared fixture helper.** Every one
   of these six defects would have been caught on day one by a single reusable
   "the vendor marker is still in the bytes" check applied to every path that
   writes SACM. `EditFixture` is the natural home. Make the property structural
   instead of per-site, and the seventh instance cannot land.
3. **`AppState::load_file` should call `RebuildDerivedViewsFromLibrary`.** It is
   the last inline copy of that derivation, and the copy-drift failure mode is
   already documented on this row (`BridgeViaLegacy`).
4. **Audit-chain coverage for flipped commands generally, not just undo.** Every
   test that pairs a command with `VerifyProject` currently uses a legacy,
   document-less fixture. The library-primary path — the one that actually runs —
   has no convergence assertion anywhere in the suite.
