---
slice: phase-9-stage-7-new-project-seed
date: 2026-07-25
verdict: FAIL
requirements: [SACM23-LIB-002]
commit: 2da697d + working tree (branch sacm23/libsacm-gsn-context-preservation)
verifier: sacm-conformance-verifier
---

## Verification result

FAIL — SACM23-LIB-002 remains `implemented`.

This was the row's first verification pass. The slice's own change (the
new-project seed) is sound, well-tested and non-vacuous; the row failed on the
*rest* of its claim. The findings were **measured**, not reasoned: the verifier
built probes in a scratchpad linked against the already-built libraries and
called `core::commands::BridgeLegacyMutationToLibrary` directly.

All three Blockers have since been fixed — see the Resolutions column and the
tests added in response. Kept per [README.md](README.md): a verification history
containing only passes is evidence of selective recording, not of quality.

## Scope

- **Requirement IDs:** SACM23-LIB-002 only. INT-001, INT-002, COMPAT-001 and
  COMPAT-002 were not ruled on and this record must not be cited for them.
- **Library files inspected:** `libs/sacm/src/io/xmi_writer.cpp`
  (`write_namespace_declarations`, `write_preserved_content`, `save_xmi_string`),
  `libs/sacm/src/compare/semantic_compare.cpp`,
  `libs/sacm/src/validation/validate.cpp`,
  `libs/sacm/include/sacm/validation/codes.h`.
- **CLI/tooling files inspected:** `tools/sacm/check_conformance_matrix.py`
  (5/5 OK), `cmake/check_layer_gates.cmake` (passes), `sacm_cli`
  (`validate`, `export`).
- **Adapter/app files inspected:** `src/sacm_adapter/library_load.{h,cpp}`,
  `src/core/project_service.cpp`, `src/core/app_state.cpp`,
  `src/core/library_package_projection.{h,cpp}`,
  `src/core/commands/library_bridge.{h,cpp}`,
  `src/core/commands/command_bus.{h,cpp}`,
  `src/core/commands/element_commands.cpp`,
  `src/core/audit/audit_recovery.cpp`, `src/core/audit/strategy_migration.cpp`,
  `src/core/audit/audit_snapshot.cpp`, `src/core/derived_views.cpp`,
  `src/app/commands/dispatch.cpp`, `src/core/project_file_io.cpp`.
- **Tests run or reviewed:** full suite **781/781 pass**; all six
  `SaveFromLibrary.SACM23_LIB_002_*`,
  `ProjectServiceTest.SACM23_LIB_002_NewProjectSeedIsStrictSacm23Xmi`,
  `LibraryPrimaryEditFlip.*`.
- **Independent probes:** direct execution of
  `BridgeLegacyMutationToLibrary` over three real documents. No repo file was
  created, edited or deleted.

## Findings

| Severity | Requirement ID | Finding | Resolution |
|---|---|---|---|
| **Blocker** | SACM23-LIB-002 | `BridgeLegacyMutationToLibrary` rebuilds the **live** library document from `sacm::serialize_sacm(core::project_library_package(document))` — an AF projection. Every command routed through `ApplyLibraryPrimaryOrLegacy` (all text edits, terminology, ACP, package removal, tree reorder, proposal, gid) therefore serializes a projection round-trip. Measured: vendor fixture `acme:owner` + `acme:vendorMetadata` present before / **absent after**; `fixture_acp_parity.sacm.xml` **10 → 0** `assuranceForge.acp`, saved bytes 3412 → 1419. The projection used is the *audit* one, which never calls `copy_library_tags_onto_package`. The note's "the library is now the serialization source of truth end-to-end" was false for these commands. | **Fixed.** Bridge now projects with `project_library_package_with_tags`. Pinned by `SaveFromLibrary.SACM23_LIB_002_BridgedEditPreservesAcpTaggedValues` (reproduced 10 → 0 before the fix). |
| **Blocker** | SACM23-LIB-002 | Same mechanism, harder failure: on `data/open-autonomy-safety-case.sacm.xml` the bridge **fails** — `project_library_package` collapses 4 argument packages into 1, duplicating artifact-reference ids (~30 `SACM-ID-001`), so the re-load is rejected. Every bridged command was broken on the repository's flagship case. Invisible to `LibraryPrimaryEditFlip.*`, whose fixtures have one argument package and whose hashes are computed through the same collapsing projection on both sides. | **Fixed** by the same change. Pinned by `SaveFromLibrary.SACM23_LIB_002_BridgedEditSucceedsOnMultiArgumentPackageCase` (reproduced the exact failure text before the fix). |
| **Blocker** | SACM23-LIB-002 | Stale Notes sentence: "the tolerant writer … does not re-declare their foreign namespace prefix … `semantic_compare` does not cover `preserved_attributes`" — both false, and already false at 2da697d. Closed under SACM23-COMPAT-001; the sentence outlived the fix. | **Fixed.** Sentence removed and replaced with what is actually open. |
| **Major** | SACM23-LIB-002 | "The last non-library producer of SACM bytes is now gone too" overstated: `strategy_migration.cpp` still writes a trusted baseline from `sacm::serialize_sacm(package)` on the normal path, and its stated justification (snapshots loaded by the legacy parser) is contradicted by `LoadSnapshotModels`, which already prefers the library. | **Fixed.** Sentence qualified to the working-file path and the migration baseline named; the stale `LoadSnapshot` comment corrected in place with the actual reason it remains (it normalizes a projection at a point where no library document exists). |
| **Major** | SACM23-LIB-002 | Call-site audit: `app_state.cpp:116` justified (guarded, visible warning); `audit_recovery.cpp:105` justified (guarded, pinned by a test); `strategy_migration.cpp:212` justified in kind. `command_bus.cpp:117` **not justified as described** — it is the *routine* path for every unflipped command, not a fallback, and the following `reload_document` overwrites the live document with those lossy bytes. | **Fixed.** Note reworded to state that the bus uses the package projection whenever the flip did not engage, and what that costs (preserved vendor content lost from disk *and* from the in-memory document). Recorded as the remaining work on the row. |
| Minor | SACM23-LIB-002 | `SaveFromLibrary.SACM23_LIB_002_RepeatedSavesAreByteStableForRepositoryCases` exists, carries the ID and passes, but was not cited. | **Fixed** — cited. |
| Minor | SACM23-LIB-002 | Stale comment at `command_bus.h:71`: "The interactive app sets this false". Nothing under `src/app/` assigns `allow_library_primary`; `dispatch.cpp` says the flip "is live again". The flip **is** live, so the data loss above was user-reachable. | **Fixed.** |
| Info | SACM23-LIB-002 | `EscapeXmlAttribute` in `project_service.cpp` is dead — its only caller was the removed literal. | **Fixed** — removed. |
| Info | SACM23-LIB-002 | The strict seed serializes as `<argumentElement xsi:type="sacm:Claim">`, which legacy `parser::parse_sacm_xml` does not treat as a relevant element, so `core::ComputeSacmHashes` records element/semantic/relationship hashes over an **empty** element set. Pre-existing for every saved file since Stage 6. | Open — follow-up. Route `ComputeSacmHashes` through the library. |

### What the slice got right (verified positively)

- **The seed test is genuine.** Confirmed independently: `sacm_cli validate
  --strict` on the *old* literal fails with `SACM-XMI-002` and `SACM-XMI-003`,
  while the tolerant load returns `VALID` — exactly the "a tolerant load passed
  either way" story. The byte-comparison against `save_xmi_string` of the same
  model is a real fixed-point assertion, not a tautology.
- **Layer boundary holds.** `library_load.h` exposes only strings and an opaque
  `LibraryDocument`; `project_service.cpp` includes only that header; the
  configure-time gate passes.
- **Failure is explicit** — `AddSacmFile` returns false rather than falling back
  to the old dialect.
- **The fallback-sentence correction was correct** — `AppState::load_file` has no
  legacy-parser fallback.
- **"All nine audit canonical-hash sites"** — counted, exactly nine.

## Matrix updates allowed

- **May mark verified:** nothing.
- **Must remain open:** `SACM23-LIB-002` stays `implemented`.

## Follow-up slice suggestions

1. **Retire the bridge.** It exists to reproduce legacy mutator results for audit
   convergence; the cost is a full projection round-trip of the live document on
   nearly every edit. Native seams for terminology and ACP would remove the
   largest remaining projection dependency in the save path.
2. **Make the convergence tests able to see loss.** `LibraryPrimaryEditFlip.*`
   compares hashes computed through the same projection on both sides, so vendor
   content is invisible to them by construction. Assertions on the saved *bytes*
   would have caught both blockers — which is how the new tests are written.
3. **Route `core::ComputeSacmHashes` through the library.**
4. **A negative test for the seed path** — the "Could not generate a SACM 2.3
   seed document" branch is untested.
