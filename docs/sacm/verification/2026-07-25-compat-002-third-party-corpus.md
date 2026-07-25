---
slice: compat-002-third-party-corpus
date: 2026-07-25
verdict: PASS
requirements: [SACM23-COMPAT-002]
commit: 269a06c + follow-up text corrections
verifier: sacm-conformance-verifier
---

## Verification result

PASS — round 4. `SACM23-COMPAT-002` may be `verified`.

This row was held at `implemented` through three prior passes on one gap, in the
verifier's own words: *"its requirement demands files produced by an independent
tool; CI parses none."* Every fixture was our own reconstruction of a dialect,
which can only prove the reader handles the shape we **believe** a tool emits.

The round-3 FAIL is recorded alongside:
[2026-07-25-compat-002-third-party-corpus-round-3-FAIL.md](2026-07-25-compat-002-third-party-corpus-round-3-FAIL.md).

## Scope

- **Requirement IDs:** SACM23-COMPAT-002; SACM23-INT-001 (non-gating closure
  only); SACM23-COMPAT-001 (regression only).
- **Library files inspected:** `libs/sacm/src/io/xmi_reader.cpp`
  (foreign-container branch), `libs/sacm/include/sacm/validation/codes.h`.
- **CLI/tooling:** `sacm_cli` (`validate`, `validate --strict`,
  `roundtrip --compat`) on all three fixtures;
  `tools/sacm/check_conformance_matrix.py`.
- **Adapter/app:** `src/core/app_state.{h,cpp}`,
  `src/ui/panels/sacm_viewer_panel.cpp`.
- **Fixtures and docs:** all seven files in
  `libs/sacm/tests/data/interop-thirdparty/`, the conformance matrix, the
  interop corpus doc, the diagnostics catalog.
- **Tests run:** `ctest -C Release` → **798/798 pass**, 2 visible SKIPs (the
  opt-in corpus pair).

## What changed

**Foreign container roots.** Real toolchains ship SACM embedded in a larger
container — an ODE `DDIPackage`. Strict refuses such a file (clause 2.4 defines
the interchange roots); refusing it on the tolerant path too meant a user holding
a real vendor file could not open their own assurance case. A tolerant load now
reads the SACM out and reports `SACM-XMI-009`, stating that the file does not
conform, that the container's other content is not represented, and that saving
writes conformant SACM rather than the original. This was a user decision, taken
explicitly after the round-3 report that two of five located files were declined.

**Real third-party bytes in CI.** Three files, unmodified, with full licence
text: `mobstr-safetycase.integration` (EPL-2.0),
`sysmline-easyexample.assurancecase` (EPL-2.0), `deis-etcs.model` (MIT).

## Findings

Round-3 exit conditions, each re-checked literally rather than accepted:

| # | Condition | Result |
|---|---|---|
| 1 | Fixtures tracked | **Met, and verified more strongly than claimed.** The verifier re-fetched all three from `raw.githubusercontent.com` at the cited commits — all match. It then *tested* the `.gitattributes` claim rather than reading it: `core.autocrlf` is `true` on this machine, and a fresh `git clone` still yields byte-identical files. Without `* -text` those hashes would break on checkout; the guard is load-bearing and works |
| 2 | EPL-2.0 licence text included | **Met.** `LICENSE.EPL-2.0.txt` is byte-identical to MobSTr's upstream `LICENSE`; `LICENSE.MIT.txt` to DEIS's. NOTICE cites §3.2(b) with the correct obligation and discharges §3.1(a) |
| 3 | NOTICE corrected; DEIS vendored | **Met, and the new claims measured.** MobSTr's root has exactly one child; `architecture_`/`failureLogic_` appear only as unused declarations. DEIS ETCS has three `odeProductPackages` siblings totalling 398 + 1 + 104 = **503** elements — the NOTICE number is exact, not rounded |
| 4 | Matrix cites this record | **Met** |

Non-gating Majors from round 3:

| Item | Result |
|---|---|
| Test adequacy | **Met.** `EXPECT_EQ(claims, 34)` (source has exactly 34 `argumentation_:Claim`); the `\|\|` split into `EXPECT_EQ(CITE-001, 40)` / `EXPECT_EQ(MULT-001, 40)` — the source has 46 constraint-bearing elements, zero `isAbstract`, 6 preserved `gsn_:Context`, and 46 − 6 = 40 reconciles exactly. The verifier's own preserved-count suggestion was wrong (`preserved_element_ids()` is 54, counting every id inside a subtree); asserting 6 *fragments* with a comment explaining the trap is recorded as better than what was asked for |
| The warning reached no user | **Met.** `AppState::load_file` surfaces Warning-and-above on the success path, deduplicated by code and led by SACM-XMI-009, into `status_message` (rendered by `sacm_viewer_panel`). **No separate SACM23-INT-003 row is needed** — this is app-side delivery of a library diagnostic, squarely inside INT-001's remit, and it now has an ID-bearing test |

Text corrections required to make the row honest, all applied after the verdict:

| Severity | Finding | Fix |
|---|---|---|
| Major | The corpus doc still described the pre-fix state in three places, and vendoring the DEIS file is what made two of them false — "the two EPL-2.0 files", the DDI register row saying "Not committed", and a gap heading reading "two tools". A reader auditing licence obligations through that doc would never learn an MIT file is redistributed | All three updated: three committed files across three projects, DDI row marked Committed under MIT |
| Major | The false "alongside architecture and failure-logic models" sentence was fixed in NOTICE.md but survived verbatim in `test_interop_corpus.cpp`, and the file header still said the corpus is "deliberately not committed" | Both corrected |
| Minor | The matrix said "Two files are committed … under EPL-2.0" and three sentences later "`deis-etcs.model` (MIT) is the third" | Reworded to three files, two licences |
| Minor | `SACM23_COMPAT_002_ThirdPartyContainerLosesItsNonSacmSiblings` was not cited, though the row's prose claim rests on that test alone | Cited |
| Minor | The load-warnings test was not cited on INT-001, so the behaviour closing the round-3 finding was invisible from the matrix | Cited, with a sentence on the row |
| Minor | The DEIS test used `EXPECT_GT(claims, 0)` — the loose form the same file argues against 100 lines earlier | Pinned to `EXPECT_EQ(claims, 9)` |
| Minor | `load_warnings` was documented "cleared by every load" but cleared inside the success branch, so a failed load left the previous file's warnings standing | Cleared at function entry |

## Matrix updates allowed

- **May mark verified:** `SACM23-COMPAT-002`.
- **Must remain open:** nothing new. Three follow-ups stand, none blocking: the
  `interchange_package_kind` / `read_root` case-sensitivity asymmetry;
  quantifying the dropped-element count inside SACM-XMI-009; and the clause-8.6
  mandatory-`name` gap in `validate.cpp` (SACM23-BASE-001) — MobSTr passes
  partly because that check does not exist, so "validates with zero errors" is
  in part a measure of what the validator does not yet check.

## Follow-up slice suggestions

1. **Quantify the SACM-XMI-009 loss in the diagnostic.** "503 elements outside
   the SACM packages are not represented" turns an abstract warning into a
   number the user can weigh. The DEIS fixture makes it assertable.
2. **A load-warnings surface beyond the status line.** `load_warnings` holds all
   of them; the status line shows one. For a non-conformant file the honest UI is
   a banner or a problems-panel section — the status line is correct but easy to
   miss on a file the user is about to overwrite.
3. **A licence-register consistency check in CI.** Assert that every file in
   `interop-thirdparty/` has a NOTICE entry, every SPDX id named there has a
   `LICENSE.*.txt` beside it, and the corpus register marks committed files as
   committed. That would have caught two of this round's findings automatically.
4. **Papyrus output remains the standing gap** (decision #20). Three projects is
   a floor; the corpus doc says so and should keep saying so.
