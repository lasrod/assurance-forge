---
slice: lib-002-resolution
date: 2026-08-08
verdict: FAIL
requirements: [SACM23-LIB-002]
commit: abcf65d8370a0f3d1c77331f18740156dd3caf1d
verifier: sacm-conformance-verifier
---

## Verification result

FAIL — SACM23-LIB-002 must remain at `implemented`.

The verification question, framed by #295 outcome 2 (separate library
interchange conformance from application editing coverage): does the evidence
support `verified` under the constraint *"No path may silently delete or
reinterpret typed SACM content. An unsupported application edit must refuse
visibly and leave the document unchanged"*? The refusal design is real,
correctly implemented for element-level unrepresentables, visibly surfaced
(banner + status bar via `AutosaveFailedEvent`), and byte-preserving on
refusal. The constraint is nonetheless falsified **by measurement** — a probe
compiled against the Release static libraries running the application's exact
call sequences — on two paths the guard does not reach.

## Scope

- Requirement IDs: SACM23-LIB-002 (primary); SACM23-INT-001 read, unaffected,
  stays `verified`; AF-STD-011 (capability matrix) found citing the wrong
  files.
- Library files inspected: none changed by the slice; read for context:
  `libs/sacm/tests/data/sacm23/argumentation-full-valid.sacm.xmi`,
  `src/sacm_adapter/library_load.cpp`.
- Adapter/application files inspected: `src/core/commands/library_bridge.cpp`,
  `command_bus.cpp`, `element_commands.cpp`, `undo_command.cpp`,
  `src/core/sacm_argument_sync.cpp`, `src/core/app_state.cpp`,
  `src/core/library_package_projection.cpp`, `src/sacm_adapter/case_projection.cpp`,
  `projection_diff.cpp`, `src/core/audit/event_replayer.cpp`,
  `audit_recovery.cpp`, `src/app/commands/dispatch.cpp`,
  `src/app/areas/canvas_history_overlay.cpp`, `status_bar_area.cpp`.
- Tests run: targeted `tests.exe` filter 41/41 pass (every test the row cites,
  including refusal, counter-preservation and the known-lost-kinds sweep);
  `sacm_tests.exe` 1/1. Full suite 1197/1201 pass, 2 skipped (opt-in interop
  corpus), 2 failed (`DraftExampleProject.*` — environmental: the `examples`
  submodule is locally modified, not a HEAD regression). Two out-of-band probe
  measurements against the Release static libraries.

## Findings

| Severity | Requirement ID | Finding | Required fix |
|---|---|---|---|
| Blocking | SACM23-LIB-002 | **Probe (a):** live `RemoveElement` with `NodeOnly` routes around the bridge (`element_commands.cpp` gates the library seam on `NodeAndDescendants`); the bus then writes `library_xmi_from_package` projection bytes to the tracked file and rebuilds the live document from them. Measured on `argumentation-full-valid.sacm.xmi`: `ArgumentGroup`, `AssertedArtifactSupport`, `AssertedArtifactContext`, the nested `ArgumentPackage`, `metaClaim` and `structure` deleted from the tracked file **and** the live document, zero diagnostics. Reachable from the element context menu. Also a live/replay divergence: the replayer bridges NodeOnly through the guarded bridge and would refuse the same event. | Route the live NodeOnly mutation through `BridgeLegacyMutationToLibrary` (as the replayer already does), pinned by a test over `argumentation-full-valid.sacm.xmi` asserting refusal + tracked-file byte-identity + live-document content. |
| Blocking | SACM23-LIB-002 | **Probe (b):** a conforming document carrying only `AssertedInference@metaClaim`, `ArgumentReasoning@structure` and an empty nested `ArgumentPackage` — every element representable — passes the element-level guard, bridges successfully, and silently loses all three. The row's sentence "It now REFUSES such a command rather than dropping content silently" is measurably overstated. | Preserve `metaClaim`/`structure` in `RebuildSacmArgumentPackageFromParser` (the projection already carries `meta_claim_refs`) and catch the empty nested package in the guard; pin byte-level over the probe-(b) shape. |
| Minor | SACM23-LIB-002 | No-bus dispatch passes no library document; every edit then re-derives the document from the projection unguarded. Reachable only when audit-bus construction fails; the entry is announced but the data-loss consequence is not. | Pass the library document into the no-bus context, or disclose the degraded mode on the row. |
| Minor | SACM23-LIB-002 | The cell's "swept mechanically … cannot silently grow" overstates the sweep: it is element-kind-level; the attribute and package-structure items are hand-maintained. | Qualify the sentence. |
| Minor | SACM23-LIB-002 | `sacm-integration-preservation.md`'s closing LIB-002 paragraph is stale in both directions (pre-guard claims not past-tensed; flip conditions superseded). | Reconcile at flip time. |
| Minor | AF-STD-011 | Capability-matrix row cites `projection_diff.cpp` and `test_projection_coverage.cpp` for the refusal claim; neither is the refusal. The claim is falsified for the probe-(b) shape. | Cite `library_bridge.cpp` and `test_save_from_library.cpp`; qualify until Blocking 2 lands. |
| Env | — | `DraftExampleProject.*` failures come from the locally modified `examples` submodule (user work in progress), not from HEAD. | Restore or land the submodule state before the round-4 regression statement. |

## Matrix updates allowed

- May mark verified: nothing.
- Must remain open: SACM23-LIB-002 at `implemented`. Round-4 pass conditions:
  1. Live NodeOnly (and any bus command mutating the legacy package with a
     library document present) refuses or preserves, pinned as in Blocking 1.
  2. The probe-(b) shape preserved (byte-level test), empty nested
     `ArgumentPackage` preserved or refused.
  3. The refusal sentence made true by 1+2 or explicitly qualified.
  4. The preservation record's closing paragraph reconciled.
  5. AF-STD-011 citations corrected, claim qualified or made true.
  6. The two `DraftExampleProject` failures resolved.
- Evidence sound and citable now: the refusal test, counter-preservation test,
  and the known-lost-kinds sweep.

## Follow-up slice suggestions

- Empty the sweep's rejected-fixtures list (`artifact-full-valid.sacm.xmi`).
- Extend the sweep below kind level: fingerprint `isCounter`, `metaClaim`,
  `structure`, `assertionDeclaration` per element.
- Pin live/replay agreement for NodeOnly (`VerifyProject` after a live NodeOnly
  removal on a rich document).
- Retire or document `allow_library_primary`: nothing in `src/` sets it false,
  yet it defines a production unflipped branch.
