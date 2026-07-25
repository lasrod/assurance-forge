---
slice: phase-10-gsn-context-preservation
date: 2026-07-25
verdict: PASS
requirements: [SACM23-COMPAT-001]
commit: 2da697d + working tree (branch sacm23/libsacm-gsn-context-preservation)
verifier: sacm-conformance-verifier
---

## Verification result

PASS — for SACM23-COMPAT-001 only, and conditionally: the verifier's second pass
returned FAIL with two Medium findings against COMPAT-001 and stated the row
"may be marked verified **conditional on both Medium findings landing in the same
edit**". Both landed (see Findings). SACM23-COMPAT-002 is correctly held at
`implemented`.

Two passes were run. The first returned FAIL with six findings; all were
addressed, and the second pass re-checked each rather than accepting the summary.
One of those fixes introduced a narrow new hole, which the second pass found by
probe and which is fixed here.

## Scope

- **Requirement IDs:** SACM23-COMPAT-001 (certified), SACM23-COMPAT-002
  (reviewed, held). Regression-checked: SACM23-XMI-003, SACM23-XMI-004,
  SACM23-RT-001, SACM23-VAL-001.
- **Library files inspected:** `libs/sacm/include/sacm/model/element.h`,
  `.../model/document.h`, `.../validation/codes.h`, `.../io/options.h`,
  `.../metadata/namespaces.h`, `libs/sacm/src/io/xmi_reader.cpp`,
  `.../io/xmi_writer.cpp`, `.../validation/validate.cpp`,
  `.../compare/semantic_compare.cpp`, `.../model/access.h`,
  `.../commands/commands.cpp`, `.../metadata/namespaces.cpp`.
- **CLI/tooling files inspected:** `libs/sacm/tools/sacm_cli.cpp` (probe driver),
  `tools/sacm/check_conformance_matrix.py` (run).
- **Adapter files inspected:** none — the uncommitted INT/LIB work in the same
  tree is out of scope and this record must not be cited for it.
- **Docs inspected:** `docs/sacm/sacm-interop-corpus.md` (new),
  `sacm-conformance-matrix.md`, `sacm-diagnostics-catalog.md`,
  `sacm-decisions-and-questions.md`.
- **Tests run or reviewed:** `cmake --build build --config Release` (exit 0,
  layer gate passed) → `ctest --test-dir build -C Release` → **781/781 passed**;
  `--gtest_filter=*COMPAT*` → 16/16 at time of the pass, 18/18 after the two
  tests added in response. Independent probes (scratchpad only) covering
  multi-fragment-in-one-role, cross-role fragments, fragment-last-in-role, mixed
  preserved/missing targets under both modes, and the legacy XMI URI.

## Findings

| Severity | Requirement ID | Finding | Resolution |
|---|---|---|---|
| Medium | SACM23-COMPAT-001 | The row's own Notes ended "Not yet `verified`: … Needs SACM23-COMPAT-002." A `verified` row that says "Not yet verified" is precisely the matrix rot this regime exists to catch. It was also a scope error: COMPAT-001's requirement is *mode separation*, not external evidence — external evidence is COMPAT-002's requirement, which is why the two were split. The sentence predated that split. | Fixed. Replaced with a scope pointer stating that third-party evidence is tracked under SACM23-COMPAT-002 and that this row covers strict/compatibility mode separation. |
| Medium | SACM23-COMPAT-001 | The mode boundary was pinned on export only. All four `SACM23_COMPAT_001_*` tests plus `SACM23_XMI_004_...` assert tolerant-load-preserves + strict-save-refuses. Nothing asserted that a **strict load** refuses vendor content and preserves nothing — the import half of "not default strict behaviour". The behaviour was correct but unpinned, which is what the ID-bearing-test rule exists to exclude. | Fixed. Added `SACM23_COMPAT_001_StrictLoadRefusesVendorContent`: asserts `!ok`, an Error diagnostic, and that `preserved_content()`, `preserved_attributes()` and `preserved_element_ids()` are all empty — the last part being what proves strict does not warn-and-keep. Covers both a foreign element and a foreign attribute, which travel different reader paths. |
| Medium | SACM23-COMPAT-001 | (Round 1) The positional-preservation claim was false for this row's own case: an unknown vendor element whose tag is no containment role is recorded with an empty role and is still appended. | Fixed. Matrix note narrowed to "only for fragments occupying a containment role the writer emits", and recorded as gap #6 in the corpus doc. |
| Medium | SACM23-COMPAT-002 | (Round 1) The positional fix restores document order but not EMF path *numbering*: `assign_emf_path_ids` indexes by raw feature name while `PreservedFragment.index` counts by normalized role, and the re-emitted fragment keeps its source spelling. | Documented rather than changed, as gap #5. The verifier endorsed this on re-review: rewriting the tag would mutate preserved content ("verbatim" being a stronger and more checkable property), while emitting typed siblings under the source spelling would break export determinism (SACM23-XMI-002) in compat mode. Recommended follow-up: give the deferred trade a decision-record ID. |
| Low | SACM23-COMPAT-002 | The round-1 fix to `collect_preserved_ids` opened a narrow hole: it compared `resolve_prefix(prefix) != kXmi` (the pinned URI, exact) while the library's own `is_xmi_namespace()` also accepts the older `http://www.omg.org/XMI` — the URI **both** EMF/GSN corpus fixtures actually declare. Only their prefix spelling saved them. Probe: a preserved element identified by `x:id` under the old URI reported `SACM-REF-001`, reinstating exactly the false "structurally broken" failure the slice exists to prevent. | Fixed. `collect_preserved_ids` now uses `metadata::namespaces::is_xmi_namespace(...)`, keeping the bare-`"xmi"` fallback for undeclared prefixes. `SACM23_COMPAT_002_PreservedTargetDowngradeDoesNotMaskRealDangling` extended with an `x:id`-under-the-legacy-URI case. |
| Low | SACM23-COMPAT-002 | The mint guard added in round 1 was untraceable and untestable-by-accident: a collision emits two elements with the same `xmi:id`, and neither `build_index` nor `validate` walks preserved content, so no existing test could go red if the guard were removed. | Fixed. Added `SACM23_COMPAT_002_MintedIdDoesNotCollideWithPreservedContent`, confirmed to fail with the guard removed and pass with it restored, and cited `commands.cpp` in the COMPAT-002 row. |
| Low | SACM23-COMPAT-002 | (Round 1) `collect_preserved_ids` matched any attribute whose local name was `id`, so a vendor `acme:id` could downgrade a genuinely dangling reference. | Fixed and pinned by the `ghost_1` case in the boundary test. |
| Low | SACM23-COMPAT-002 | (Round 1) The REF-001 → REF-003 downgrade boundary was untested. | Fixed by `SACM23_COMPAT_002_PreservedTargetDowngradeDoesNotMaskRealDangling`: one document with a preserved target, a genuinely missing target and a vendor-id-only target; one code asserted per target, an Error still required, and strict asserted to emit no REF-003. |
| Info | SACM23-COMPAT-002 | `code_for()` in the boundary test matches on diagnostic *message* text. Correct today; silently re-targets if a future diagnostic mentions the id earlier. | Open. Match on `problem.affected` instead. |

### Claims checked and upheld

- **Strict mode is unchanged.** `preserve_extension_subtree` returns before
  recording when `reader.strict()`, so `preserved_element_ids` is empty on a
  strict load and both `check_references` and `validate_structure` take the
  pre-change branch. Probed: strict validate still reports `SACM-REF-001` for
  both a preserved and a missing target.
- **Positional re-insertion holds** for multiple fragments in one role, fragments
  spanning roles under one parent, and a fragment last within its role while
  other roles follow. All three `ROUNDTRIP OK` in compat mode. The anchor-snapshot
  arithmetic (`slot = index - claimed` against a pre-insertion snapshot) is
  correct as written, and the fragment records the *normalized* role, matching the
  writer's key.
- **Nothing silently accepts previously-rejected data** beyond the intended
  REF-003 downgrade, which is reachable only after a tolerant load, only for ids
  physically present in the source, and only on documents strict save already
  refuses. Warning is the right severity; tolerant load `ok` is unchanged.
- **Library and layout boundaries intact.** `PreservedFragment` is
  `{string, string, size_t}`; no pugixml in public headers; no GSN or layout
  vocabulary; layer gate passed.
- `SACM-REF-003` is in the diagnostics catalog at the correct severity.

## Matrix updates allowed

- **May mark verified:** `SACM23-COMPAT-001` — both Medium conditions landed in
  the same edit. Evidence is complete for its requirement: legacy versions (EMF
  reference dialect, 2.2 per-package namespaces), third-party XMI variation
  (prefix independence, namespace normalization), vendor extensions (elements and
  attributes), mode separation in both directions, and a `semantic_compare` that
  covers `preserved_attributes` so the round-trip assertions cannot pass
  vacuously.
- **Must remain open:** `SACM23-COMPAT-002` (`implemented`) — blocked solely on
  corpus gap #1: its requirement demands files *produced by an independent tool*,
  and CI parses none. The verifier confirmed the corpus gap list states this
  adequately, and noted that the honesty is what blocks the row, "which is the
  system working". `SACM23-INT-001`, `SACM23-INT-002`, `SACM23-LIB-002` were not
  examined in either pass.

## Follow-up slice suggestions

1. **Third-party bytes in CI** — the only thing that unblocks COMPAT-002. An
   opt-in CTest gated on an external corpus directory (`SACM_INTEROP_CORPUS=<dir>`,
   `GTEST_SKIP` when absent) keeps the redistribution rule intact while making the
   evidence reproducible for anyone holding the file.
2. **An `is_xmi_id_attribute(reader, attr)` helper** — this was the second time an
   ad-hoc namespace comparison diverged from `is_xmi_namespace`. One shared helper
   closes the class rather than the instance.
3. **Decision record for gap #5**, so the verbatim-vs-path-readability trade is a
   recorded choice rather than a deferred one.
4. **Preserved-content awareness in duplicate detection** — the mint guard fixes
   the write side; `build_index` still cannot see a source document that ships a
   preserved id duplicating a typed one.
