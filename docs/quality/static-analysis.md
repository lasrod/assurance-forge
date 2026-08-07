# Static analysis

What clang-tidy checks, why those checks and not the others, and what the
current findings are. Companion to the [code quality policy](code-quality-policy.md),
which covers compiler warnings and formatting.

Before this existed, the [baseline](repository-baseline.md) recorded static
analysis as *not configured* — no clang-tidy, no cppcheck, no findings, and
therefore no claim about the codebase in either direction.

| Field | Value |
|---|---|
| Tool | clang-tidy 22.1.4 |
| Configuration | [`.clang-tidy`](https://github.com/lasrod/assurance-forge/blob/main/.clang-tidy) at the repository root |
| Runner | `tools/quality/run_clang_tidy.py` |
| Machine-readable | [clang-tidy-baseline.json](clang-tidy-baseline.json) |
| Scope | 255 production translation units |
| Findings | 90 |
| Runtime | ~9 minutes at 14-way parallelism |

## Running it

```bash
python tools/quality/run_clang_tidy.py            # analyze and write the baseline
python tools/quality/run_clang_tidy.py --check    # fail if findings grew
python tools/quality/run_clang_tidy.py --report   # print every finding
python tools/quality/run_clang_tidy.py --all-checks   # measure what the exclusions cost
```

The project must have been configured at least once (`cmake --preset default`),
because the analysis needs the FetchContent dependency headers under
`build/_deps`. If they are absent the runner says so and exits rather than
analyzing a subset and reporting it as a clean result.

While iterating on one file, `--check --filter <substring>` analyzes only
matching paths. It narrows coverage, so it cannot write a baseline and does not
gate a merge; the runner refuses the combination that would let it.

## Choosing the checks

The selection principle: **a check earns its place by finding defects in this
codebase**, not by being enabled in someone else's.

Every family was run over all 255 translation units before being kept or
dropped. The four enabled families with no exclusions produce **4,784**
findings; the enabled set produces **90**. The difference is not leniency —
nine checks account for 4,694 of the raw count (98%), five of them for 4,586
between them, and each is wrong here for a reason:

Re-derive the counts rather than trusting this table:

```bash
python tools/quality/run_clang_tidy.py --all-checks
```

| Excluded check | Count | Why |
|---|---|---|
| `misc-include-cleaner` | 3,357 | Demands include-what-you-use. `CLAUDE.md` states the opposite policy: `SortIncludes` is disabled and include order is preserved by hand, because Windows and some third-party headers have order dependencies. Enforcing it would break the build it protects. |
| `misc-const-correctness` | 756 | Real but purely stylistic. 756 edits across every subsystem is the "broad stylistic churn" [#293](https://github.com/lasrod/assurance-forge/issues/293) exists to avoid. A candidate for a later deliberate pass. |
| `misc-non-private-member-variables-in-classes` | 241 | `ui::UiState`, `core::AppState` and the SACM element structs are deliberately public data aggregates. Accessors carrying no invariant are not an improvement. |
| `bugprone-easily-swappable-parameters` | 141 | Its own documentation concedes a high false-positive rate. Here it fires on `(id, name)` string pairs whose names already distinguish them. |
| `performance-enum-size` | 91 | Micro-optimization of enum underlying types, with no measured motivation in a GUI application. |
| `bugprone-exception-escape` | 50 | **Measured, not assumed.** It fires on implicitly-generated move constructors of aggregate structs holding `std::string`/`std::vector` (`SacmElement`, `MultiLangText`, `ReviewProposal`) and on `main()`. It found no hand-written `noexcept` function that can throw, which is the defect the check exists for. |
| `misc-no-recursion` | 28 | This codebase walks trees — assurance tree, GSN layout, XMI nesting. Recursion is the design. |
| `misc-use-anonymous-namespace` | 28 | Flags `static` where the function already sits inside an anonymous namespace. Removing a redundant keyword at 28 sites is naming-only churn. |
| `clang-analyzer-optin.performance.Padding` | 2 | Reordering `ui::UiState`'s 38 fields to save padding would scramble a struct people read, for memory nobody has measured a need for. |

An unexplained exclusion is indistinguishable from a check nobody dared turn
on, which is why each carries its count and its reason. Any of these can be
reinstated; the argument against each is recorded so it can be contested.

## Current findings

90 findings, none yet fixed. They are baselined so the count cannot grow, and
triaged into a follow-up rather than fixed here — configuring a tool and
changing behaviour are separate responsibilities, and the second needs tests.

### By area

| Area | Findings |
|---|---|
| `src/core` | 34 |
| `src/ui` | 25 |
| `src/app` | 20 |
| `src/bridge` | 3 |
| `libs/sacm` | 2 |
| `src/mcp` | 2 |
| `src/sacm_adapter` | 2 |
| `src/ai` | 1 |
| `src/export` | 1 |

`libs/sacm` contributing 2 of 90 is worth noting: the reusable library every
SACM conformance claim rests on is the cleanest part of the tree by this
measure. `src/core`, `src/ui` and `src/app` hold 79 of the 90 between them,
which is roughly what their share of the code predicts — this ranks nothing on
its own, and is not a defect density.

### The ones that look like defects

Not all 90 are bugs. These are the subset where the check describes a
correctness problem rather than a style or performance preference:

| Finding | Where | Risk |
|---|---|---|
| Unchecked `std::optional` access (×12) | `draft_workspace_store.cpp` ×3, `app_state.cpp` ×3 + header, `proposal_actions.cpp`, `app_runtime_io.cpp`, `canvas_history_overlay.cpp`, `element_edit_controller.cpp`, `mcp/server.cpp` | Dereferencing an empty optional is undefined behaviour. These sit in draft and project state — the data this tool exists to not lose. |
| `bugprone-unused-return-value` | `core/project_service.cpp:78` | A result that "should not be disregarded" is disregarded. Unchecked errors are named explicitly in #293's scope. |
| `bugprone-empty-catch` | `core/confidence/confidence_store.cpp:428` | An exception is swallowed silently. |
| `bugprone-return-const-ref-from-parameter` (×2) | `app/app_runtime.cpp:500,524` | Use-after-free if the argument is a temporary. |
| `bugprone-incorrect-roundings` (×2) | `ui/gsn/gsn_canvas.cpp:682`, `ui/panels/history_timeline_panel.cpp:87` | `(double + 0.5)` cast rounds incorrectly for negatives. |
| `bugprone-inc-dec-in-conditions` | `ui/gsn/gsn_terminology_card.cpp:318` | Modification and reference in one condition; evaluation order. |
| `bugprone-implicit-widening-of-multiplication-result` | `bridge/transport.cpp:35` | A multiplication in `unsigned int` widened to `size_t` afterwards — overflow before the widening. |
| `bugprone-narrowing-conversions` | `core/project_file_io.cpp:153` | `size_type` to `streamsize`, implementation-defined. |

None is confirmed to be a live bug — a static-analysis finding is a question,
not a verdict, and several may be unreachable in practice. Confirming or
dismissing each is the follow-up's job.

## The ratchet

`--check` compares against the committed baseline and fails when a count grows.

### What runs when

A full sweep is far too slow to sit in front of every pull request. The first
attempt at the CI job ran all 255 translation units on a 4-core runner and was
still going after 30 minutes, against a repository whose median CI wall clock
is 12.7 minutes. So scope depends on the trigger:

| Trigger | Scope |
|---|---|
| Pull request | Only the `.cpp` files the branch touched (`--paths`) |
| Push to `main` | The full sweep — the authoritative one |
| Local | Whatever you ask for; `--filter` for one file |

The gap that leaves is worth naming rather than discovering: **a pull request
that edits a header can add findings in `.cpp` files it did not touch**, and
those are not analyzed until the full sweep runs after the merge. Closing it
before the merge would mean resolving the include graph to find every affected
translation unit, which this does not do.

A branch that changes no `.cpp` file analyzes nothing and passes. That is the
correct outcome, not a broken run — but it is why the sweep on `main` is the
one to believe.

It keys on **(file, check)**, deliberately not on line numbers. Line numbers
move whenever anything above them is edited, so a line-keyed baseline would
fail on unrelated changes and teach people to regenerate it without reading it —
which is how a gate stops being a gate.

Consequences of that choice, stated rather than discovered later:

- Adding a *new* instance of a check to a file that already has one **is**
  caught, because the count rises.
- Moving an existing finding within a file is not caught, correctly.
- Removing one finding and adding another of the same check in the same file
  nets to zero and is **not** caught. This is the known hole. It is the price
  of a baseline that survives ordinary editing.

Findings are never auto-fixed. `FormatStyle: none`, and no `--fix` anywhere: a
tool rewriting safety-case handling code unattended is not a trade this project
makes.

### A translation unit that fails to analyze

clang-tidy reports a translation unit it could not compile on stderr and exits
non-zero, while stdout stays empty. Reading stdout alone would take that as
"no findings here" — so a missing include path or define could make the ratchet
pass on code nothing actually looked at, which is the one way a green gate is
worse than no gate.

The runner therefore checks the exit status of every translation unit and
**aborts the whole run** if any failed, naming each one and the diagnostic that
caused it. It does not write a baseline from a partly-failed run, because every
later comparison would then be against a baseline that recorded unanalyzed code
as clean.

All 255 currently analyze cleanly, so this guards a failure that has not
happened rather than one that has.

### Verifying the gate can fail

A gate that has never failed is a gate nobody has tested. This one was verified
by breaking the code on purpose: an `(int)(2.5 + 0.5)` cast was added to
`assurance_tree.cpp`, `--check` reported

```
src/core/assurance_tree.cpp: bugprone-incorrect-roundings went from 0 to 1
```

and exited 1. The cast was then removed and the check passed again. The same
was verified for `--paths`, which is what the pull-request job actually runs,
rather than assuming the two share a code path.

The incomplete-run guard was verified the same way: an `#include` of a header
that does not exist produced

```
1 translation unit(s) could not be analyzed:

  src/bridge/transport.cpp
      exit 1: ...error: 'this_header_does_not_exist_af.h' file not found
```

and exited 2 — rather than reporting the file clean, which is what it did
before the check existed.

## Known gaps

Listed rather than omitted, because a gap nobody has written down reads as a
gap nobody has.

| Gap | Detail |
|---|---|
| **Windows only** | The baseline is generated with clang targeting the Windows build, so `#ifndef _WIN32` branches are **not analyzed**. This is the mirror image of the compiler-sweep problem in the [code quality policy](code-quality-policy.md#checking-other-compilers-diagnostics-without-waiting-for-ci): a platform that is not analyzed is not clean, it is unmeasured. |
| **Production only** | `tests/` and `libs/sacm/tests/` are excluded. A different risk profile, and including them would roughly triple runtime for findings nobody ships. Revisit under [#292](https://github.com/lasrod/assurance-forge/issues/292). |
| **Toolchain headers dropped** | Findings outside the repository are discarded. MSVC's own `<filesystem>` implementation contributed three; they describe someone else's code and would pin the baseline to whoever generated it. |
| **CI's clang-tidy may differ from the baseline's** | The baseline is generated by 22.1.4. The CI job uses the runner image's LLVM, because Chocolatey has no 22.1.4 package to pin to. A different version runs different checks, so the CI job prints both versions and the runner warns when they disagree. An **older** clang-tidy finds fewer checks and the ratchet still passes — meaning the gate is then weaker than this page describes, not that the code is cleaner. Aligning them exactly is open. |
| **No cppcheck** | A second opinion would catch what clang-tidy misses. Not configured. |
| **Not a CTest gate** | At ~9 minutes locally (longer on a 4-core CI runner) it does not belong in the suite developers run constantly. It runs as its own CI job. |
| **Pull requests see only changed `.cpp` files** | See [What runs when](#what-runs-when). A header edit can add findings elsewhere that only the full sweep on `main` catches. |

## Reproducing

Prerequisites: Python 3.10+, clang-tidy on `PATH` (or `CLANG_TIDY` set to it),
and a configured build tree.

```bash
cmake --preset default        # once, so build/_deps exists
python tools/quality/run_clang_tidy.py
```

Unlike the [repository baseline](repository-baseline.md), which describes one
commit and is expected to go stale, **this baseline is meant to stay current**.
It is checked on every pull request, and it should be regenerated whenever
findings are fixed so the improvement is locked in.
