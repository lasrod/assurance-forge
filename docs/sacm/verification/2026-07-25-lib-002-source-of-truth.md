---
slice: phase-9-stage-7-source-of-truth
date: 2026-07-25
verdict: PASS
requirements: [SACM23-LIB-002]
commit: 2da697d + working tree (branch sacm23/libsacm-gsn-context-preservation)
verifier: sacm-conformance-verifier
---

## Verification result

PASS — round 3. Rounds 1 and 2 both FAILED and are recorded alongside this file:

- [2026-07-25-lib-002-source-of-truth-round-1-FAIL.md](2026-07-25-lib-002-source-of-truth-round-1-FAIL.md)
- [2026-07-25-lib-002-source-of-truth-round-2-FAIL.md](2026-07-25-lib-002-source-of-truth-round-2-FAIL.md)

Three rounds found three genuine defects. The third was reachable only because the
second fix was incomplete in a place the existing tests were structurally unable to
see — which is the durable lesson of this row and is captured in follow-up 3.

The verifier did not accept the green suite as evidence at any round: it rebuilt
probes against the updated libraries and re-measured every prior failure through
the application's real APIs.

## Scope

- **Requirement IDs:** SACM23-LIB-002 only. INT-001, INT-002, COMPAT-001 and
  COMPAT-002 were not examined in any round of this row; this record must not be
  cited for them.
- **Library files inspected:** `libs/sacm/include/sacm/compat/preserve.h`,
  `libs/sacm/src/compat/preserve.cpp`, `libs/sacm/include/sacm/model/document.h`,
  `libs/sacm/src/model/access.h`, `libs/sacm/src/model/document.cpp`
  (revision guard).
- **CLI/tooling files inspected:** `tools/sacm/check_conformance_matrix.py`
  (5/5 OK), `cmake/check_layer_gates.cmake` (run directly — the incremental build
  did not re-trigger it, so the verifier ran it itself: passed).
- **Adapter/app files inspected:** `src/core/commands/library_bridge.{h,cpp}`,
  `src/core/audit/event_replayer.cpp` (`BridgeViaLegacy` delegation),
  `src/sacm_adapter/library_load.cpp`, `src/core/audit/audit_recovery.cpp`,
  and the LIB-002 matrix row re-read end to end.
- **Tests run or reviewed:** full suite → **790/790 pass**. 14 ID-bearing tests
  are cited on the row; all exist and pass.
- **Independent probes:** the round-2 probe set rebuilt and re-run against the new
  binaries. No repo file created, edited or deleted by the verifier.

## Findings

The blocking findings are recorded in the two FAIL records. Round 3 closed them:

| # | Round-3 condition | Result |
|---|---|---|
| 1 | `BridgeViaLegacy` fixed | **Met, and better than asked.** It now delegates to `core::commands::BridgeLegacyMutationToLibrary` with an optional `rederive_failure_context` preserving the replayer's "failed at transaction N event M" message. The duplicated algorithm was deleted rather than the fix copied into it, which removes the mechanism that caused round 2. |
| 2 | Restore keeps ACP tags | **Met.** Verifier's own end-to-end probe on `fixture_acp_parity.sacm.xml`: `assuranceForge.acp` **10 → 10** (round 2: 10 → **0**). |
| 3 | Restore keeps unknown content | **Met.** Vendor fixture: `acme:vendorMetadata` 1 → 1 and `acme:owner` 1 → 1 (round 2: both → **0**). |
| 4 | Restore succeeds on the multi-package case | **Met.** `data/open-autonomy-safety-case.sacm.xml` restores with `argumentPackage` count 8 → 8 (round 2: `Replay failed: Bridge re-derive (reload_document) failed at transaction 1 event 1`). |
| 5 | "end-to-end" sentence removed | **Met.** Replaced with a scoped claim that defers to the remaining-work statement instead of contradicting it. |
| 6 | `adopt_preserved_content` carries `preserved_element_ids` | **Met.** Library probe: the rebuilt document now reports `warning [SACM-REF-003]` where round 2 measured `ERROR [SACM-REF-001] … missing element 'c1'`. The `target.find(id) == nullptr` guard is correct — an id the rebuild produced as a real typed element is not demoted to opaque. |
| 7 | Mutation contract documented | **Met.** `document.h` and `access.h` both name the third door and bound it to compatibility content. |
| 8 | Library-level test | **Met.** Covers adoption by id, the restored SACM-REF-003 verdict, foreign-namespace copy, non-overwrite, and that a source-only id invents no element, with a non-vacuity guard that the stand-in intermediate really is lossy. |

Remaining, non-blocking:

| Severity | Requirement ID | Finding | Required fix |
|---|---|---|---|
| Info | SACM23-LIB-002 | `src/core/audit/event_replayer.cpp` now includes `core/commands/library_bridge.h` out of the file's otherwise alphabetical order. `SortIncludes` is disabled repo-wide, so this is noise rather than a violation. | None. |
| Disclosed | SACM23-LIB-002 | The command bus's UNFLIPPED path (NodeOnly removal, undo, no-document dispatch) still writes projection bytes and reloads the live document from them, so an unflipped command costs preserved vendor content on disk and in memory. Raised as round-1 item 6, where the verifier asked for accurate disclosure rather than a fix. | Follow-up 1. Now stated on the row. |
| Disclosed | SACM23-LIB-002 | A bridged edit normalizes each claim's Description into the legacy two-slot form once. Idempotent and canonical-hash-neutral, but a rewrite the user did not ask for. Logged as Info in round 2 and explicitly declined as a condition. | Follow-up 2. Now stated on the row. |

### The three new restore tests are genuine

They edit the **Content** field — the one that bridges on *both* sides, where Name
is a native seam on replay, which is exactly how round 2's hole hid. They assert
`restored.lossy_fallback_warning` is empty, so they cannot pass by taking the
degraded projection path. They carry non-vacuity guards. The implementer's
revert-and-confirm-failure check matches what the verifier measured independently
before the fix existed.

### Is revision-neutral adoption the right call?

Yes, and checked rather than assumed. The verifier traced every consumer of
`Document::revision()`: it is read only by `Document::apply`'s `expected_revision`
guard and by the library's own tests. Nothing in the library or in Assurance Forge
uses it as a general "has this document changed" token — the app keeps its own
`AppState::case_revision`. Bumping it would be worse: it would spuriously
invalidate a preview whose typed model is untouched.

One condition would change that answer: if `revision()` ever becomes the key for a
*byte*-level cache (serialized output, file hashes), adoption would silently
produce stale entries, because it does change the bytes. Worth a header sentence
if that ever ships.

## Matrix updates allowed

- **May mark verified:** `SACM23-LIB-002`, conditional on two editorial
  corrections landing in the same edit — both applied: the "Remaining, and the
  reason this row is not `verified`" sentence rescoped to "Remaining work,
  disclosed and not blocking verification", and the "Remaining before `verified`"
  sentence replaced with a citation of this record.
- **Must remain open:** nothing for this row.

## Amendment — 2026-07-25, after the SACM23-INT-001 slice

`verified` **stands.** The INT-001 slice fixed two further instances of the
self-rebuild pattern (`AppState::sync_library_document` and the command bus's
Stage-5 net), which strictly increases the set of paths where the library rather
than a projection owns the model — the property this row asserts. No cited test
regressed; the verifier re-ran the full suite (792/792) rather than trusting the
count, and measured the change with its own probe.

The fix falsified **two** sentences in this row's note, and the second was not
noticed by the implementer:

- **Understatement.** "an unflipped command still costs preserved vendor content
  both on disk and in memory" — half wrong. Measured after an unflipped command:
  the tracked file is 391 bytes with `acme:owner` and `vendorMetadata` gone, while
  the in-memory document is 501 bytes with both intact.
- **Overstatement, and the more dangerous.** "all three sites produce identical
  bytes for the same state, so `manifest.last_known_raw_file_hash` stays valid
  across an autosave/explicit-save mix." Measured: `autosave bytes ==
  explicit-save bytes: 0`. This divergence is *new* — before the Stage-5 fix both
  sides were degraded and therefore agreed. A reader trusting it would conclude
  the save sites cannot diverge.

Both are corrected in the row, with the divergence stated as benign **and why**:
`VerifyProject` still succeeds, because audit verification converges on the
canonical hash and preserved content does not enter it; the stale raw hash
self-corrects at the next audited command.

On evidence ownership: the Stage-5-net test stays cited on SACM23-INT-001 only,
cross-referenced in this row's prose. A `SACM23_INT_001_*` name in this row's
Tests column would make the cell disagree with the test that populates it, and a
duplicate LIB-002-named test guarding the same behaviour would drift — which is
the lesson of `BridgeViaLegacy`.

The verifier's closing point, recorded because it inverts the usual assumption:

> An understating note is *worse* than an overstating one in this codebase,
> because nobody audits a claim that sounds modest. The round-2 bridge defect
> survived its pass for exactly that reason — the note said the bridged path was
> fixed, so nobody looked at the twin.

Remaining on this row: the unflipped autosave, now the single site still writing
projection bytes to the tracked file. Closing it collapses both corrected
sentences back into one unqualified claim.

## Follow-up slice suggestions

1. **Close the unflipped command-bus path** — the last place an Assurance Forge
   projection decides persisted bytes. With
   `reload_document_keeping_compatibility_content` in hand the shape is clear:
   on the unflipped path, re-derive the document *keeping* compatibility content
   and serialize the document, instead of writing projection bytes to disk.
2. **Retire the bridge for text edits.** The native seam exists; what keeps the
   bridge is legacy two-slot `content`/`description` parity. Closing that removes
   the Description normalization and the last structural reason for the round trip.
3. **Guard against the blindness, not just this instance.** Every convergence test
   hashes through `project_library_package` on both sides, so no hash comparison
   can ever see tag or preserved-content loss — this row's entire defect class is
   invisible to them by construction. A single byte-level invariant in the bus
   ("the preserved-content carrier count and ACP count never decrease across a
   command") would catch the class rather than the three cases now pinned.
4. **Route the strategy-migration baseline through a library document**, retiring
   the last legacy writer, as that code's own comment now proposes.
