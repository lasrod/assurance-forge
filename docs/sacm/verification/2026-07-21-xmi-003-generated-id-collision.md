---
slice: xmi-003-generated-id-collision
date: 2026-07-21
verdict: PASS
requirements: [SACM23-XMI-003]
commit: fbbaa3c
verifier: sacm-conformance-verifier
---

## Verification result

PASS

Strengthening of an already-`verified` row. The reader now pre-reserves every
explicit `xmi:id` before minting any `generated_N`, closing a real duplicate-id
round-trip failure without weakening genuine duplicate detection.

## Scope

- Requirement IDs: SACM23-XMI-003 (references / duplicate-id diagnostics; the row
  this fix touches)
- Library files inspected: `libs/sacm/src/io/xmi_reader.cpp` (id generation
  `generate_id`, `used_ids`, `collect_existing_ids`, all three parse-time id
  inserts, `read_root`, `build_index`, `load_impl`), `libs/sacm/src/validation/validate.cpp`
  (`validate_structure` duplicate detection)
- CLI/tooling files inspected: `tools/sacm/check_conformance_matrix.py` (ran);
  CLI duplicate-id CTests referenced in `libs/sacm/CMakeLists.txt`
- Adapter files inspected: none — change is purely internal to the reader (no
  adapter/CLI/layout/public-header files in the commit)
- Tests run or reviewed: `libs/sacm/tests/test_xmi_io.cpp` (new tests + duplicate-id
  + broken-ref); rebuilt `sacm_tests` and ran full suite plus
  `--gtest_filter=*SACM23_XMI_003*`; `check_conformance_matrix.py` (all gates green)

## Findings

| Severity | Requirement ID | Finding | Required fix |
|---|---|---|---|
| Info | SACM23-XMI-003 | Fix is correct and complete. `collect_existing_ids` uses the same `find_attr_local(node, "id")` accessor as the three parse-time `used_ids` inserts and as `generate_id`'s consumer, so it pre-reserves precisely the id attributes that matter (xmi:id preferred, then any `:id` suffix, then plain `id`, excluding xmlns). `generate_id` is the only minting path, so no path escapes the pre-reservation. EMF paths are rewritten to real id attributes earlier (`normalize_emf_references`) and are then collected by the same pre-pass. | none |
| Info | SACM23-XMI-003 | Placement correct: after root null-check, before `read_root`, inside both the `<XMI>`-wrapper and bare-root branches. | none |
| Info | SACM23-XMI-003 | Adversarial check passes: pre-reservation feeds only `used_ids`, which gates only `generate_id`. Real duplicate detection is independent — `build_index` (`try_emplace`) and `validate_structure`. The genuine-duplicate fixture (`ids-duplicate-invalid.sacm.xmi`, two explicit `xmi:id="claim_1"`) is still reported: `SACM23_XMI_003_ReportsDuplicateIds` passes. No false-negative path introduced. | none |
| Info | SACM23-XMI-003 | Tests genuinely gate: names embed the requirement id; the reader-boundary test fails (duplicate) with the fix line removed and passes with it (falsification-confirmed); the round-trip test loads → saves → reloads and semantically compares. | none |
| Info | SACM23-XMI-003 | Matrix update is honest: adds only the new test names and an idempotency/detection-unchanged note; cited paths exist; `sacm_matrix_check` green. | none |

Regression: full library suite green (includes RT-001/RT-002 round-trip and the
losslessness gate). Losslessness/boundary unaffected — the change only alters
which synthetic id an otherwise id-less element receives; no standard data is
dropped, no public header or layout/GSN vocabulary touched.

## Matrix updates allowed

- May mark verified: SACM23-XMI-003 retains the two new test names and the note
  (strengthening of an already-verified row, backed by ID-bearing tests that fail
  without the fix).
- Must remain open: none newly opened by this change.

## Follow-up slice suggestions

- The verifier suggested a positive `load → save → load` round-trip test; added as
  `SACM23_XMI_003_PersistedGeneratedIdRoundTripsCleanly`, mirroring the
  `core::library_xmi_from_package` pipeline whose failure surfaced the bug.
- Optional further hardening: assert determinism of generated-id assignment across
  two loads of the same id-less document (same element → same `generated_N`), to
  lock idempotency in as a contract rather than an incidental property.
