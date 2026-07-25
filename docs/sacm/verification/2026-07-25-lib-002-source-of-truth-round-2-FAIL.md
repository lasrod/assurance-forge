---
slice: phase-9-stage-7-source-of-truth
date: 2026-07-25
verdict: FAIL
requirements: [SACM23-LIB-002]
commit: 2da697d + working tree (branch sacm23/libsacm-gsn-context-preservation)
verifier: sacm-conformance-verifier
---

## Verification result

FAIL. Superseded by
[2026-07-25-lib-002-source-of-truth.md](2026-07-25-lib-002-source-of-truth.md).

All eight round-1 exit conditions were met and independently re-measured. The row
still could not flip: **the fix had been applied to one of two twins.**

`core::commands::BridgeLegacyMutationToLibrary` was corrected;
`core::audit::BridgeViaLegacy` — the audit replayer's copy of the same
project-mutate-reload round trip, used by 13 event branches — was not. Since
`RestoreSacmFromAudit` serializes the document replay produces, round 1's three
failures were still live on the **restore** save site, which the row explicitly
claims.

Measured end to end through the application's own APIs: after one bridged
*content* edit, a restore wrote a file with **0 of 10** ACP TaggedValues and **0
of 2** vendor markers and reported **no degradation warning**; on the repository's
flagship case the restore **failed outright**.

## Scope

Library, tooling, adapter and app files as in the round-3 record, plus
`src/core/audit/event_replayer.cpp` (`BridgeViaLegacy`, `ApplyEventToLibrary`,
`ReplayToLibrary`). Full suite **786/786 pass** at the time. Independent probes:
round-1's bridge probes re-run against the rebuilt libraries, plus a
no-op-bridged-edit byte differ, a canonical-hash comparator, an end-to-end
`EnsureAuditStore → bus edit → tamper → RestoreSacmFromAudit` probe, and a
library-only probe for `adopt_preserved_content`.

## Findings

| Severity | Requirement ID | Finding | Resolution |
|---|---|---|---|
| **Blocker** | SACM23-LIB-002 | `core::audit::BridgeViaLegacy` still did `project_library_package` + plain `reload_document` — character for character the code just fixed in the live bridge — across 13 event branches (UpdateElementText Content/Description, terminology, ACP, NodeOnly removal, gid, tree reorder/move). Measured: `fixture_acp_parity.sacm.xml` live save at `assuranceForge.acp`=10 but **restore at 0**; vendor fixture markers 1 → **0** on restore; `data/open-autonomy-safety-case.sacm.xml` restore aborts with `Replay failed: Bridge re-derive (reload_document) failed at transaction 1 event 1 (UpdateElementText)` — a project with one content edit in its log could not be recovered at all. `restored.lossy_fallback_warning` was **empty** in every case, so the restore presented itself as fully faithful, and the audit verifier could not detect it either (the canonical hash drops the same tags on both sides). The existing `SACM23_LIB_002_RestoreFromAuditPreservesUnknownContent` passed only because `CreateChildElement` replays through the **native** seam. | **Fixed by deleting the twin.** `BridgeViaLegacy` now delegates to `BridgeLegacyMutationToLibrary`, which gained an optional `rederive_failure_context` to keep the replayer's location message. Pinned by three `SACM23_LIB_002_RestoreAfterBridgedEdit*` tests, each confirmed to fail with the delegation reverted. |
| **Major** | SACM23-LIB-002 | The Notes contradicted themselves: the bolded "The library is now the serialization source of truth end-to-end." stood alongside a later statement that an unflipped command "still costs preserved vendor content both on disk and in memory". Under the matrix preamble that bolded sentence is a full-compliance claim the row did not support. | **Fixed.** Rescoped to the save sites and flipped commands, deferring to the remaining-work statement. |
| **Major** | SACM23-LIB-002 | `sacm::compat::adopt_preserved_content` did not carry `Document::preserved_element_ids_`, so a rebuilt-and-adopted document validated with `ERROR [SACM-REF-001] 'ctx_1' references (target) missing element 'c1'` even though its saved output carried the `gsn:Context` fragment — reintroducing the exact false "structurally broken" verdict `SACM-REF-003` was added in this same tree to prevent. The API restored the bytes but not the knowledge that makes them legible. | **Fixed.** Adoption now carries `preserved_element_ids` for ids the target does not already resolve, with the header comment updated. |
| Minor | SACM23-LIB-002 | The new public library function had **no test under `libs/sacm/tests/`** — covered only indirectly by three Assurance Forge tests. For a library whose selling point is independence, a standalone consumer had zero coverage of it. | **Fixed.** `Sacm23RoundTrip.SACM23_LIB_002_AdoptPreservedContentRestoresWhatAProjectionDrops`. |
| Minor | SACM23-LIB-002 | `document.h` ("Mutation: exclusively through preview/apply") and `access.h` ("mutation only via `Document::apply` or load … mechanically enforced") asserted an invariant the library no longer had. The verifier judged the third door itself defensible — opaque compatibility content only, and the `const_cast` well-defined because elements are owned non-const — but leaving the headers asserting otherwise was not. | **Fixed.** Both name the door and its contract; the revision-neutrality rationale is stated. |
| Info | SACM23-LIB-002 | Pre-existing and not caused by the fix: a *no-op* bridged edit rewrites every claim's Description structure once (2 → 4 `<description>` elements on `fixture_acp_parity.sacm.xml`, each statement gaining a duplicate carrying `lang="en"`). Idempotent and canonical-hash-neutral; verified to predate the fix. Raised as information, explicitly **not** a condition. | Disclosed on the row; follow-up. |
| Info | — | `tools/sacm/check_conformance_matrix.py` was changed to scan `tests/` as well as `libs/sacm/tests`. Reviewed as sound and not gaming: check 1 was previously unsatisfiable for integration rows, and the change makes check 2 *stricter* by also policing requirement IDs in the app test tree. | None. |

### Round-1 checklist: all eight met, re-measured

Conditions 1–3 (ACP tags, unknown content, multi-package case through the *live*
bridge) were re-measured with the verifier's own probes and confirmed fixed.
Conditions 4–8 (the residual-gap sentence, the "last non-library producer"
qualification, the `library_xmi_from_package` sentence, the missing test citation,
the `command_bus.h` comment) were all confirmed applied.

### Canonical hash: checked directly, not inferred

`CanonicalModelHash(project_library_package(document))` either side of a bridged
edit is byte-identical, and identical again after a second pass. Every hash site
goes through `library_canonical_hash*`, which re-loads and re-projects with
`project_library_package` at hashing time, so what the bridge uses internally
never reaches a hash.

### On fairness

The verifier recorded that every round-1 condition was met, and that the blocker
was a function it had not named in round 1 — so it could reasonably read as a new
bar. It raised it anyway because it is the same defect reachable through a save
site the row explicitly claims, because a recovery operation silently destroying
every Assurance Claim Point was the most serious data-loss finding across the
rounds, and because "restore from audit" losing data is exactly the failure the
audit subsystem exists to prevent. Scope was not otherwise expanded: the
Description duplication was logged as Info, not as a condition.

## Matrix updates allowed

- **May mark verified:** nothing.
- **Must remain open:** `SACM23-LIB-002` stays `implemented`.
