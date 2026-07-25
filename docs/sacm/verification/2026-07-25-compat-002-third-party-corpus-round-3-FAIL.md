---
slice: compat-002-third-party-corpus
date: 2026-07-25
verdict: FAIL
requirements: [SACM23-COMPAT-002]
commit: 3678e31 + working tree
verifier: sacm-conformance-verifier
---

## Verification result

FAIL — superseded by
[2026-07-25-compat-002-third-party-corpus.md](2026-07-25-compat-002-third-party-corpus.md).

**The substance was already done**: the gap this row had been held on for two
rounds — *"its requirement demands files produced by an independent tool; CI
parses none"* — was genuinely closed, and the verifier confirmed it against
upstream rather than against the NOTICE. **No reader change was required.** Four
mechanical items blocked the flip.

Kept per [README.md](README.md): a verification history containing only passes is
evidence of selective recording, not of quality.

## Scope

Library, CLI, adapter, fixture and doc files as in the superseding record, plus
`third_party/sacm-2.3/SACM-2.3-formal-23-05-08.pdf` (clauses 8.5, 8.6). Full
suite 796/796 at the time.

## What the verifier confirmed independently

| Claim under review | Verdict | How |
|---|---|---|
| The files are genuinely third-party and unmodified | **True** | sha256 of each vendored file byte-identical to `raw.githubusercontent.com` at the cited commit |
| Both upstream repos are EPL-2.0 at those commits | **True** | GitHub API `spdx_id`; `LICENSE` fetched at each commit is EPL-2.0 verbatim |
| The ~80 SysMLine errors are genuine, not reader bugs | **True** | Spec p.30 clause 8.6: *"ImplementationConstraints should only be specified if +isAbstract is true"*; p.29 clause 8.5: *"For each of the LangString in the value feature, their +lang must be unique."* Source has 46 constraint-bearing elements, **zero** `isAbstract`, duplicated `lang="language"`. 46 − 6 (inside preserved `gsn_:Context` subtrees) = **exactly the 40 + 40 reported.** The arithmetic closes |
| MobSTr round-trips losslessly | **True** | All 132 lang-bearing nodes and all 80 relationship endpoints match across load → compat-save → reload |
| SysMLine round-trips losslessly | **True** | 184/184 lang-bearing nodes; all 6 `gsn_:Context` fragments re-emitted; the reload reproduces the identical 40+40 errors and 14 REF-003 warnings |
| Plural-role strip cannot mis-type anything | **True** | No SACM class name ends in `s` (whole name table checked), so stripping can never turn one valid class into another. Fires only after normal resolution fails, tolerant-only, gated by `is_package_kind` |

## Findings

| Severity | Finding | Resolution |
|---|---|---|
| **Blocking** | `libs/sacm/tests/data/interop-thirdparty/` was **untracked**. "Committed and run in CI" was not yet true; a fresh clone ran neither third-party test | Committed in 269a06c, plus `.gitattributes` `* -text` — the verifier later found `core.autocrlf` is `true` here, so without it a fresh clone would have broken the byte-for-byte claim |
| **Blocking (legal)** | EPL-2.0 §3.2(b) requires *"a copy of this Agreement … included with each copy of the Program."* NOTICE.md gave only a hyperlink | `LICENSE.EPL-2.0.txt` and `LICENSE.MIT.txt` committed; NOTICE cites §3.2(b) and §3.1(a) and points at the files |
| **Blocking (overstatement)** | NOTICE claimed the MobSTr file embeds SACM *"alongside architecture and failure-logic models"* — **false**. Its `DDIPackage` has exactly one child; the `architecture_`/`failureLogic_` prefixes are declared but unused. So the committed corpus never exercised the loss `SACM-XMI-009` exists to disclose. Compounding it, the matrix called the DEIS ETCS models "redundant" when DEIS is **MIT** (redistributable) and its `etcs.model` is the file that actually carries 503 non-SACM elements | Took the verifier's preferred alternative: vendored `deis-etcs.model` (MIT) and added `SACM23_COMPAT_002_ThirdPartyContainerLosesItsNonSacmSiblings`, which asserts the siblings are present in the source, absent from the save, and the SACM survives |
| **Blocking (matrix rot)** | The row read `verified` while its own note ended *"Reviewed (not certified) in … which holds this row at `implemented`."* A verified row citing a record that declines to verify it is exactly the rot the regime exists to prevent — and `check_conformance_matrix.py` does not catch it, since it checks tests and paths, not records | Citation replaced |
| **Major (non-gating)** | Test adequacy: both tests asserted only `EXPECT_GT(claims, 0)`. A regression dumping 33 of 34 claims into preserved content would still pass, and `semantic_compare` could not catch it either since both sides degrade equally | Exact counts pinned |
| **Major (non-gating)** | **The SACM-XMI-009 warning reached no user.** `AppState::load_file` discarded diagnostics on the success path. Before this change an ODE container was refused with a visible error and nothing was lost; after it, the file opened silently and Save rewrote it | `AppState::load_file` now surfaces Warning-and-above, deduplicated by code and led by SACM-XMI-009, with an ID-bearing test |
| Minor | `interchange_package_kind` resolves xsi:type case-insensitively while the recursive `read_root` resolves it case-sensitively | Open follow-up |
| Minor | SACM-XMI-009 does not quantify the loss — "content outside the SACM packages is NOT represented" for 503 dropped elements | Open follow-up |
| Minor | Matrix said "no change to the reader beyond foreign-container support"; the row itself documents the GSN/EMF reader work these dialects drove | Qualified to "no further reader change in this slice" |
| Minor (other row) | `validate.cpp` never checks the mandatory `name: LangString[1]` (clause 8.6). MobSTr's package has no `name` and still validates clean — so "validates with zero errors" is partly a measure of what the validator does not check | Recorded against SACM23-BASE-001; explicitly not a blocker here |

## Matrix updates allowed

- **May mark verified:** none this pass.
- **Must remain open:** `SACM23-COMPAT-002` → revert to `implemented`.
