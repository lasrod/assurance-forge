---
slice: strategy-migration-preserves-unknown-content
date: 2026-07-26
verdict: FAIL
requirements: [SACM23-LIB-002]
commit: 0ea27584181683a516ada2bdd8356663e440506a
verifier: sacm-conformance-verifier
---

## Verification result

FAIL

**All four re-verification conditions from the round-1 record are met.** I checked
each one literally and independently, and re-falsified the new test myself. The
`b8c2ffe` slice is correct, is not merely test-satisfying, and does not regress
anything downstream. Finding 3 is discharged; Findings 1 and 2 are discharged.

The row still cannot be marked `verified`, for one reason: I went looking for a
seventh instance of the projection-rebuild defect and found something worse than a
seventh caller. **The shared bridge every one of the six sites was routed onto —
`core::commands::BridgeLegacyMutationToLibrary` — is itself lossy for standard,
typed SACM 2.3.** Measured end-to-end on the exact bytes the command bus writes: a
single goal rename on a conforming SACM 2.3 argumentation document deletes
`AssertedArtifactSupport`, `AssertedArtifactContext`, `ArgumentGroup` and a nested
`ArgumentPackage`; drops `ArgumentReasoning@structure` and
`AssertedInference@metaClaim`; and silently clears `isCounter="true"`, converting a
rebuttal into a supporting inference. The load is diagnostic-free, so none of it is
announced.

That is `SACM23-LIB-002`'s requirement verbatim — "Assurance Forge projections must
not become the serialization source of truth" — falsified by measurement, on the
most common edit in the application. It is pre-existing (not introduced by
`b8c2ffe`, which is a strict improvement at its site) and it is not disclosed on
the row; the cell's own wording implies the opposite.

## Scope

- Requirement IDs: `SACM23-LIB-002`. Also touched by the measurement, not verified
  here: `SACM23-INT-001`, `SACM23-ARG-001`.
- Library files inspected (`libs/sacm`): **none changed by this slice.** Confirmed
  by `git diff --stat 2364d37..0ea2758` — no path under `libs/sacm/`. Read for
  context: `libs/sacm/include/sacm/model/{element,argumentation,artifact}.h`,
  `libs/sacm/src/io/{name_tables,xmi_reader,xmi_writer}.cpp`,
  `libs/sacm/include/sacm/compat/preserve.h`. The library reads and re-writes every
  construct listed above correctly — the loss is entirely application-side.
- CLI/tooling files inspected: `tools/sacm/check_conformance_matrix.py` (exit 0,
  all five checks OK), `tools/i18n/check_catalog.py` (exit 0).
- Adapter / application files inspected:
  - `src/core/audit/strategy_migration.cpp` (the slice)
  - `src/core/commands/library_bridge.cpp` / `.h`
  - `src/core/sacm_argument_sync.cpp` (`RebuildSacmArgumentPackageFromParser`)
  - `src/core/library_package_projection.cpp` / `.h`
  - `src/sacm_adapter/case_projection.cpp`, `src/sacm_adapter/library_load.cpp` / `.h`
  - `src/core/commands/command_bus.cpp` / `.h`, `element_commands.cpp`,
    `gid_commands.cpp`, `undo_command.cpp`
  - `src/core/app_state.cpp` (`load_file`, `save_file`, `sync_library_document`)
  - `src/core/audit/{audit_snapshot,audit_recovery,replay_verifier,undo_boundary,audit_store}.cpp`
  - `src/app/commands/dispatch.cpp`, `src/app/app_runtime_project.cpp`
  - `src/sacm/sacm_model.h`, `src/sacm/sacm_serializer.cpp`
- Docs inspected: `docs/sacm/sacm-conformance-matrix.md` (the whole LIB-002 cell,
  split into 49 sentences and read individually), the round-1 record,
  `docs/sacm/sacm-stage3-projection-baseline.md`, `sacm-gsn-metamodel-gaps.md`,
  `sacm-2.3-metamodel-inventory.md`, `sacm-2.4-watch.md`.
- Tests run or reviewed:
  - `ctest --test-dir build -C Release` → **807/807 passed**, 2 skipped
    (`Sacm23InteropCorpus.SACM23_COMPAT_002_*`, corpus absent — same as round 1).
  - `tests.exe --gtest_filter='StrategyMigration.*'` → 7/7 at HEAD.
  - Falsification build (fix reverted to `b8c2ffe~1`) — see below.
  - Reviewed: `tests/test_strategy_migration.cpp` (all 7),
    `tests/test_undo_command.cpp`, `tests/test_history_reconstruction.cpp`,
    the 15 `SaveFromLibrary.SACM23_LIB_002_*` names.
  - Out-of-band measurement: four standalone probes compiled against the Release
    static libs, exercising paths ctest does not reach. Working tree restored and
    confirmed clean at `0ea27584181683a516ada2bdd8356663e440506a`.

### Condition-by-condition (the round-1 contract)

| # | Condition | Result |
|---|---|---|
| 1 | Finding 3 fixed in `strategy_migration.cpp`; both writes serialize the document; the false `no library document` justification deleted | **MET.** `:229` bridges via `BridgeLegacyMutationToLibrary`, `:238` `save_document`, `:272` `const std::string& baseline_xml = migrated_xml`. No `library_xmi_from_package` or `sacm::serialize_sacm` left in the file; the `sacm/sacm_serializer.h` include is gone. The false justification is deleted and replaced with an accurate one. |
| 2 | `SACM23_LIB_002_StrategyMigrationPreservesUnknownContent` exists, asserts on the **bytes** of both artifacts, confirmed to fail before the fix | **MET and independently re-falsified.** See table below. |
| 3 | Finding 1 clause replaced with the exact wording given | **MET, verbatim.** Sentence [25] of the cell matches the required text character for character, including the `SACM23_INT_001_UnflippedBusCommandPreservesUnknownContentInTheDocument` citation. |
| 4 | Both Finding 2 edits applied; no sentence still lists undo as unflipped | **MET.** [7] now reads "an unflipped command — a NodeOnly removal, an unsupported seam —"; [8] reads "(a NodeOnly removal, a dispatch with no document)"; [25] reads "(NodeOnly removal, no-document dispatch)". `grep -n "undo" src/core/commands/command_bus.cpp` returns nothing, so the round-1 Minor on the two code comments is also closed (`c10c9d3`). |

### Falsification evidence

`src/core/audit/strategy_migration.cpp` restored to `b8c2ffe~1`, rebuilt, run,
then `git checkout --` and clean-tree confirmed.

| Reverted | Test | Result |
|---|---|---|
| Finding-3 fix only | `StrategyMigration.SACM23_LIB_002_StrategyMigrationPreservesUnknownContent` | **FAILED on all six byte assertions** — `vendorMetadata`, `Q3-2026` and `http://acme.example/toolchain` absent from *both* the tracked working file and the promoted baseline. The non-vacuity guard passed (the pre-migration file did carry the content) and `migration.migrated` was true, so the failure is the real defect and not a setup artifact. |
| Finding-3 fix only | `StrategyMigration.MigratesLegacyProjectAndVerifyConverges` | passed — correctly isolates the new test to the preservation property. |
| Finding-3 fix only | other 5 `StrategyMigration.*` | passed. |

The implementer's claim of six failing assertions is exact.

### Is the fix correct, or does it only satisfy the test?

Correct, on every axis I could check:

- **Preservation parity.** `BridgeLegacyMutationToLibrary` projects with
  `project_library_package_with_tags` (vendor TaggedValues, ACPs, per-package
  structure) and re-derives via `reload_document_keeping_compatibility_content`
  (preserved unknown XML + vendor attributes). The old path used
  `library_xmi_from_package(package)`, which is the *same* POD round-trip with
  neither restoration. The new path is a strict superset of what the old one
  preserved.
- **The migration still does its job.** `MigratesLegacyProjectAndVerifyConverges`
  passes: `VerifyProject` diverges before and converges after, the manifest's
  `replay_root_snapshot_id` is the promoted baseline, and the second run reports
  `migrated == false` (idempotence). `SkipsWhenPathIsNotTheAuditedSacm` passes.
- **The byte-identical baseline breaks nothing downstream.** `LoadSnapshotModels`
  tries the library first and only falls back to `sacm::parse_sacm`, so a
  library-XMI baseline loads *better* than the old legacy one. `FindUndoBoundary`
  and `ResolveReplayRoot` key on `transaction_sequence` only.  `VerifyProject` and
  `RestoreSacmFromAudit` both load the replay root through
  `sacm_adapter::load_document`. `manifest.last_known_raw_file_hash ==
  snapshot.raw_file_hash` is already the normal arrangement for the initial
  snapshot; nothing compares the two expecting them to differ.
- **Failure handling.** A bridge failure returns false with a prefixed message; a
  `save_document` failure returns false with the load diagnostics summarized. The
  migration aborts before touching the file in both cases.

## Findings

| Severity | Requirement ID | Finding | Required fix |
|---|---|---|---|
| Blocking | SACM23-LIB-002 | **The shared bridge destroys standard SACM 2.3 content, and this reaches disk on the most common edit in the app.** Not a seventh caller — the *one implementation* all six sites were routed onto. Measured end-to-end with a probe that runs the real `core::commands::UpdateElementTextCommand` (returns `library_primary = 1`) on `libs/sacm/tests/data/sacm23/argumentation-full-valid.sacm.xmi` and then calls `sacm_adapter::save_document(*ctx.library_document)` — exactly what `CommandBus::Execute` writes to the tracked file. Renaming one goal **deletes** `AssertedArtifactSupport` (`support_1`, clause 11.17), `AssertedArtifactContext` (`actx_1`, 11.18), `ArgumentGroup` (`group_core`, 11.2) and the nested `ArgumentPackage` (`argpkg_detail`, 11.4); **drops** `ArgumentReasoning@structure` (11.12) and `AssertedInference@metaClaim` (11.10); and **clears `isCounter="true"` on `counter_1`**, so a rebuttal of the top claim is re-serialized as an inference *supporting* it. `load_document` on that file emits **zero diagnostics**. Localised precisely: `sacm_adapter::project_case` carries all of it correctly; the loss is entirely in `core::RebuildSacmArgumentPackageFromParser` (`src/core/sacm_argument_sync.cpp:98`), which `clear()`s every list at `:127-132` and then has `if/else if` branches for only six element types — and whose `assertedinference` branch (`:165-172`) never copies `element.is_counter`, although `sacm::AssertedInference::isCounter` exists (`sacm_model.h:199`) and `serialize_sacm` writes it (`sacm_serializer.cpp:233`). On every bridged command — `UpdateElementText`, `SetElementGid`, all ten terminology commands, all four ACP commands, three package commands, two tree commands, proposals — an Assurance Forge projection *is* the serialization source of truth. It also violates the project's own hard constraint that the tool "must never silently modify or reinterpret safety arguments". Pre-existing, not introduced by `b8c2ffe`. | Two parts. **(a) Immediate, and cheap:** fix `RebuildSacmArgumentPackageFromParser` to copy `is_counter` onto `AssertedInference`/`AssertedEvidence`/`AssertedContext`. Pin with a test named `SACM23_LIB_002_BridgedEditPreservesCounterRelationships` asserting `isCounter="true"` is still in the saved **bytes** after an `UpdateElementTextCommand` on a case carrying a counter-inference. **(b) The structural half:** either extend `reload_document_keeping_compatibility_content` to carry over library elements the POD rebuild cannot express, or make the bridge refuse — fail the command with a diagnostic — when the projected package does not account for every element in the document. Pin with `SACM23_LIB_002_BridgedEditPreservesElementsOutsideThePodSubset` over `libs/sacm/tests/data/sacm23/argumentation-full-valid.sacm.xmi`. Until (b) lands, the LIB-002 cell must state the loss explicitly and name the affected classes. |
| Minor | SACM23-LIB-002 | **Sentences [40]–[43] of the LIB-002 cell describe the current code in the present tense, and they are now false** — the same drift class as Findings 1 and 2. [44] then says "Finding 3 is now fixed", so a careful reader recovers — but the cell literally asserts of today's code something I falsified in the opposite direction. | Past-tense [40]–[43]: "loaded … projected … wrote"; "were all destroyed"; "could not recover"; "the loss was unrecoverable". |
| Minor | SACM23-LIB-002 | **[44]'s "so this cannot become a seventh site" carries an assurance it does not provide, and [19] compounds it.** [19] describes the bridge as re-deriving "while restoring what no POD projection can carry (preserved unknown XML and vendor attributes)", which reads as an exhaustive list and is not. Read together the cell asserts a losslessness the code does not have. | Qualify [19] and add the Blocking finding's disclosure to the "Remaining work" paragraph [25]. |
| Minor | SACM23-LIB-002 | **[48] still reads "Verified by … 2026-07-25-lib-002-source-of-truth.md (round 3; rounds 1 and 2 FAILED …)" while the status column says `implemented`,** and it does not mention the 2026-07-26 round-1 FAIL — a *fourth* failing round. The gate does not catch this (check [5] only rejects manual evidence on `verified` rows). | Rewrite [48] to cite the latest pass plus the earlier records. |
| Minor | SACM23-LIB-002 | **`UndoCommand.SACM23_LIB_002_UndoStaysLibraryPrimaryUnderTheKillSwitch`** (added in `6795fd7`) is a real LIB-002 pin and is **not** in the cell's cited test list. I reviewed the exemption it covers and it is sound. | Add the test name to the LIB-002 evidence column. |
| Minor | SACM23-LIB-002 | **The round-1 Minor is still open: no test asserts the audit chain converges on the library-primary undo path.** Measured sound out-of-band in round 1, so this is coverage, not correctness. Does not block. | As stated in round 1. |
| Info | SACM23-LIB-002 | **`AppState::load_file` is still the last inline copy of `RebuildDerivedViewsFromLibrary`.** Round-1 Info, unchanged. Does not block. | Have `load_file` call the shared helper. |
| Info | SACM23-LIB-002 | **The two round-1 test-strength notes are unchanged and still do not affect validity**, and the legacy undo routing still has no vendor-content assertion on its saved bytes. None of these blocks. | Follow-up. |
| Info | SACM23-LIB-002 | **The new test's fixture reaches the vendor content by string-injecting into `sacm::serialize_sacm` output**, anchored on `"AssuranceCasePackage"` with an explicit guard. Fragile by nature but guarded, and it is the only way to produce content the legacy writer cannot emit. Acceptable. | None. |

### Why the Blocking finding is not goalpost-moving

The round-1 conditions were a contract and I checked them literally first: all four
are met, and I have said so above without qualification. This finding is separate,
and it is in scope for three reasons: (1) this pass was explicitly tasked with
looking for a further instance of the projection-rebuild defect from a new angle;
(2) it is the same defect, found at the root rather than at a caller — the fix all
six sites were routed onto; and (3) it falsifies the row's requirement text
directly, by measurement, with a safety-relevant consequence (`isCounter`
inversion). The angle that found it, for the next pass: not grepping for the lossy
function, but asking *which element kinds survive the projection round-trip* — dump
the projection's own inventory and compare it to what the library read.

## Matrix updates allowed

- **May mark verified:** nothing.
- **Must remain open:** `SACM23-LIB-002`, at `implemented`. Conditions for a round-3
  pass:
  1. `core::RebuildSacmArgumentPackageFromParser` copies `is_counter` onto
     `AssertedInference`, `AssertedEvidence` and `AssertedContext`, pinned by a
     byte-level test named `SACM23_LIB_002_BridgedEditPreservesCounterRelationships`
     and confirmed to fail before the fix.
  2. Either the bridge preserves typed SACM 2.3 outside the legacy POD subset, or it
     fails the command with a diagnostic when the projection cannot account for
     every element in the document — pinned by
     `SACM23_LIB_002_BridgedEditPreservesElementsOutsideThePodSubset`.
  3. If (2) is deferred rather than fixed, the LIB-002 cell discloses it explicitly,
     naming `AssertedArtifactSupport`, `AssertedArtifactContext`, `ArgumentGroup`,
     nested `ArgumentPackage`, `ArgumentReasoning@structure` and
     `AssertedInference@metaClaim`, and the application surfaces a visible warning
     on loading a file containing any of them.
  4. Sentences [40]–[43] are past-tensed; [19] is qualified; [48] cites this record.
  5. `UndoCommand.SACM23_LIB_002_UndoStaysLibraryPrimaryUnderTheKillSwitch` is added
     to the cell's evidence column.
- **May be cited as evidence now, independent of the above:**
  `StrategyMigration.SACM23_LIB_002_StrategyMigrationPreservesUnknownContent`. It is
  non-vacuous, byte-level, covers both artifacts, and I falsified it myself.
- **`SACM23-INT-001` should be reviewed, not silently left at `verified`.** Its cell
  says "The projection is proven field-complete and lossless (slice 1)"; the
  Stage-3 baseline that proves it was measured only over Assurance Forge's own
  repository fixtures, which contain none of the constructs above. The claim is true
  over that corpus and false in general.
- **`SACM23-ARG-001` is unaffected and correctly `verified`** — its claim is a
  *library* claim, and my probe confirms the library reads and re-writes all of it
  faithfully. The defect is entirely on the Assurance Forge side.
- Gates: `check_conformance_matrix.py` exit 0; `check_catalog.py` exit 0. Full suite
  807/807.

## Follow-up slice suggestions

1. **Make the projection's coverage a tested invariant, not an assumption.**
   Enumerate the element kinds `sacm_adapter::project_case` can emit, enumerate the
   kinds `RebuildSacmArgumentPackageFromParser` has a branch for, and fail a test on
   any kind in the first set missing from the second. That single test would have
   caught four of the six losses on day one, and it fails loudly the next time the
   library grows a kind.
2. **Rename the lossy reload.** Round 1 proposed this; this pass argues for it
   harder. `reload_document` vs `reload_document_keeping_compatibility_content`
   reads as "plain" vs "special", when it is really "destructive" vs "less
   destructive".
3. **Extend the Stage-3 projection baseline beyond Assurance Forge's own files.**
   Point it at `libs/sacm/tests/data/sacm23/*-valid.sacm.xmi` as well; every
   difference it reports there is a real integration gap.
4. **A byte-level preservation assertion as a shared fixture helper** (carried over
   from round 1, now with a wider marker set: a vendor element, an ACP tag, *and* a
   counter-relationship).
5. **`AppState::load_file` should call `RebuildDerivedViewsFromLibrary`** (carried
   over from round 1).
6. **Audit-chain coverage for flipped commands generally** (carried over from round
   1).
