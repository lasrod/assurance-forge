# Repository quality baseline

A point-in-time measurement of the Assurance Forge repository, taken so that the
cleanup programme in [#287](https://github.com/lasrod/assurance-forge/issues/287)
can prioritize by evidence and so later quality claims have something to be
compared against.

This page is a **snapshot, not a live dashboard**. It describes one commit. When
the numbers here disagree with the tree in front of you, the tree is right and
this page is old — regenerate it (see [Reproducing](#reproducing)) rather than
trusting it.

| Field | Value |
|---|---|
| Base commit | `84cd98c89bfc2ec461d54dc045c7748c96f68400` (tip of `main`) |
| Base commit date | 2026-08-05 |
| Collected | 2026-08-06 |
| First commit | 2026-04-17 |
| Collector | `tools/quality/collect_baseline.py` |
| Machine-readable | [repository-baseline.json](repository-baseline.json) |

A baseline cannot name its own commit — recording the SHA changes it, and
amending to correct that changes it again. It therefore cites the **base
commit**: the point this work branched from, which is stable however many
commits the baseline itself adds. The JSON additionally records the HEAD it was
generated at, for anyone who needs the exact provenance.

The figures below describe `main` at the base commit **plus the files this
baseline adds** (the collector, this report, and the JSON snapshot) — the tree as
it exists once this change lands. Only the documentation and tooling counts are
affected; no production, test, or coverage figure moves.

## How to read this

Every figure below is one of three things, and the report says which:

- **Measured** — computed from this commit's working tree or git history by the
  collector script, or read back from a named CI run.
- **Approximate** — measured, but by a method with known imprecision. The
  imprecision is stated where it applies.
- **Unavailable** — not measured, with the reason and the command that would
  produce it. See [Unavailable measurements](#unavailable-measurements). These
  are listed rather than omitted, because a gap that is invisible reads as a
  zero.

Nothing here is an estimate in the sense of a guess. Where a number could not be
obtained it is absent and named, not filled in.

## Size

Vendored, generated, and first-party content are counted separately and are
never summed into a headline total. Submodule contents are upstream code and are
excluded from all line counts entirely.

Line counts cover code files only (`.cpp`, `.cc`, `.h`, `.hpp`, `.inl`, `.py`,
`.sh`, `.ps1`). Markdown, assets, and data files contribute to file counts but
not to line counts, which is why those rows show `—`.

### By category

| Category | Files | Physical lines | Code lines |
|---|---|---|---|
| First-party production | 527 | 92,198 | 71,524 |
| First-party tests | 170 | 37,222 | 27,441 |
| First-party tooling | 19 | 4,900 | 4,162 |
| CI and repository config | 14 | 1,581 | 1,319 |
| Documentation | 86 | — | — |
| Assets and data | 13 | — | — |
| Other (repository root, AI assets) | 48 | 491 | 423 |

*Approximate:* "code lines" strips blank lines and C/C++ comments with a simple
scanner that does not understand comment markers inside string literals. It
undercounts by a small and unmeasured amount. "Physical lines" is exact.

The Documentation row counts 86 files of any type under `docs/`, which is not the
same as the 104 tracked markdown files reported under
[Documentation](#documentation) — the latter counts `.md` everywhere in the
repository, including `README.md`, `CONTRIBUTING.md`, and the agent definitions.

The table has no vendored row because **no vendored code is committed to this
repository**. Third-party code arrives two ways, both outside every count above:

- **6 submodules** — `examples`, `external/hello_imgui`,
  `external/nativefiledialog-extended`, `external/picosha2`, `external/pugixml`,
  `external/safety-case-core-guidelines`. Tracked as gitlinks; their contents are
  upstream code and are excluded from all line counts by design.
- **`third_party/`** — the normative SACM 2.3 specification and machine-readable
  model, which every conformance claim is checked against. It is **gitignored and
  fetched by script** (`bash scripts/fetch-sacm23-references.sh`), so it is not in
  the repository at all and contributes to no file or line count here. A working
  copy that has never run the fetch script is missing it entirely.

### By subsystem

| Subsystem | Files | Physical lines | Code lines |
|---|---|---|---|
| src/core | 179 | 30,769 | 23,186 |
| src/app | 122 | 21,546 | 17,067 |
| src/ui | 95 | 16,252 | 12,830 |
| libs/sacm | 44 | 9,020 | 7,124 |
| src/sacm_adapter | 10 | 3,330 | 2,297 |
| src/ai | 19 | 2,084 | 1,787 |
| src/mcp | 11 | 1,917 | 1,440 |
| src/agent | 5 | 1,696 | 1,374 |
| src/export | 9 | 1,567 | 1,312 |
| src/parser | 8 | 1,468 | 1,245 |
| src/sacm | 7 | 1,389 | 1,026 |
| src/bridge | 6 | 1,160 | 836 |
| tests (app suite) | 116 | 33,419 | 25,285 |
| libs/sacm (tests) | 13 | 3,803 | 2,156 |

`src/core` is the largest subsystem at a third of production code, which is worth
noting against the standing instruction in `CLAUDE.md` to *keep `core` small*.

## Tests

| Measure | Value |
|---|---|
| Test source files | 129 |
| gtest cases | 1,199 |
| gtest suites | 164 |
| CTest registered tests | 1,207 |

The gtest count is measured by scanning for `TEST`, `TEST_F`, and `TEST_P` in
test sources; the CTest count comes from a configured build tree. They differ by
8 because CTest also registers non-gtest entries (the matrix and catalog gates,
the MCP smoke test).

### Attribution by subsystem

Each test file is attributed to the subsystems whose production headers it
includes, resolved against the filesystem rather than guessed from the filename.
**A test file touching several subsystems is counted under each, so these rows
overlap and must not be summed.**

| Subsystem | Test files including it | gtest cases in those files |
|---|---|---|
| src/core | 86 | 866 |
| src/sacm | 36 | 380 |
| src/parser | 30 | 378 |
| src/app | 30 | 217 |
| src/sacm_adapter | 17 | 173 |
| src/ui | 16 | 161 |
| libs/sacm | 14 | 114 |
| src/ai | 6 | 40 |
| src/bridge | 4 | 40 |
| src/mcp | 3 | 40 |
| src/agent | 3 | 26 |
| src/export | 1 | 35 |

One test file (`tests/test_xml_parser.cpp`) includes no first-party production
header and is unattributed.

The `libs/sacm` row deserves attention. The reusable library is the surface every
SACM conformance claim rests on, and it is directly included by 14 test files,
while the legacy `src/sacm` parser/serializer is included by 36. This is a
statement about *where tests point*, not about whether the library is
well-tested — its coverage is the highest of any scope measured below. But it
means the conformance evidence base is concentrated in `libs/sacm/tests/` (13
files) with almost no cross-checking from the application suite, which is
relevant to [#295](https://github.com/lasrod/assurance-forge/issues/295).

### Ambiguous include prefixes

| Prefix | Served by |
|---|---|
| `sacm/` | `libs/sacm/include`, `src` |

`#include "sacm/..."` resolves to either the reusable library or the legacy
`src/sacm` subsystem depending on the rest of the path — `sacm/model/document.h`
is the library, `sacm/sacm_parser.h` is not. A reader cannot tell which
subsystem an include names without checking the filesystem. This is the concrete
form of the `src/sacm` versus `libs/sacm` confusion described in
[#291](https://github.com/lasrod/assurance-forge/issues/291).

## Coverage

Measured, and read back from Coverage workflow run
[31007382290](https://github.com/lasrod/assurance-forge/actions/runs/31007382290),
which built the base commit `84cd98c`.

**No compiled file differs between that commit and the tree measured here**, so
these figures describe exactly the code counted above. The collector checks this
rather than comparing SHAs: a commit touching only documentation cannot
invalidate a coverage number, but a SHA comparison would claim otherwise. The
list of differing compiled files is recorded in the JSON as
`compiled_files_changed_since_run`, and it is empty. Were it not, these
percentages would need re-measuring before being cited.

| Scope | Lines | Functions | Branches | Conditions (MC/DC) |
|---|---|---|---|---|
| Logic scope (headline) | 71.7% | 84.2% | 62.0% | 51.2% |
| Full `src/` | 48.6% | 60.3% | 42.1% | 36.8% |
| Core scope (`src/` minus `app`, `ui`) | 77.0% | 91.2% | 66.1% | 53.6% |
| SACM library (`libs/sacm`) | 84.6% | 93.7% | 65.7% | 54.8% |

Linux / GCC 14 only. No threshold gates apply to any of these numbers. The scope
definitions live in `gcovr-logic.cfg`, `gcovr-sacm.cfg`, and
`.github/workflows/coverage.yml`; see [COVERAGE.md](../COVERAGE.md) for why the
project publishes several views rather than one.

The gap between the full-`src/` view (48.6%) and the logic view (71.7%) is the
GUI and application-bootstrap code that has no headless tests. That gap is a
known and deliberate exclusion, not an accident, but it is also the reason a
single headline coverage number would be misleading in either direction.

## Architecture

| Measure | Value |
|---|---|
| Layers checked | 10 |
| Explicit gate exceptions | 2 |
| Enforcement point | configure time (`FATAL_ERROR`) |

The two exceptions are:

- `ui:ui/panels/preferences_panel.h=ai/`
- `ui:ui/panels/welcome_modal.h=app/`

The gate scans source-level `#include` directives. It does not check CMake target
dependencies, so a subsystem can still link against more than its headers
suggest — the distinction [#291](https://github.com/lasrod/assurance-forge/issues/291)
raises.

## Standards and capability matrices

### SACM 2.3 conformance matrix

| Status | Rows |
|---|---|
| `verified` | 32 |
| `implemented` | 1 |
| **Total** | **33** |

The single non-`verified` row is `SACM23-LIB-002`, which
[#295](https://github.com/lasrod/assurance-forge/issues/295) exists to resolve or
reclassify.

Row count is not the same as coverage of the standard. This matrix records 33
requirements the project has written down; whether that set is *complete* against
the normative specification is exactly the open question #295 poses, and this
baseline cannot answer it.

### Capability matrix

| Status | Rows |
|---|---|
| `supported` | 78 |
| `planned` | 36 |
| `prototype` | 11 |
| `candidate` | 4 |
| `in-development` | 1 |
| **Total** | **130** |

41 rows cite no tests. For `planned`, `candidate`, and `not-planned` rows that is
required by policy; the `feature_matrix_check` CTest already enforces that
`supported` rows cite tests that exist, so the untested rows are the unbuilt
ones.

By area: ENG 20, AI 18, STD 17, MOD 14, GSN 12, PLAT 12, METH 11, PAT 10, ACP 8,
DIA 8.

## Documentation

| Measure | Value |
|---|---|
| Markdown files tracked | 104 |
| Broken relative links | 0 |
| Pages in no nav and linked from nowhere | 30 |

No broken internal links — but nothing enforces that, so it is a fact about
today, not a guarantee. [#290](https://github.com/lasrod/assurance-forge/issues/290)
covers adding the check to CI.

The 30 unreachable pages are real: they are neither listed in `mkdocs.yml` nor
linked from any other tracked markdown file, so a reader can only find them by
browsing the repository tree. They are concentrated in `docs/sacm/` (prompts,
plans, policies, verification records) and include two top-level documents —
`docs/ARCHITECTURE.md` and `docs/RELEASING.md` — that are referenced by nothing
at all. Some of these are working records that legitimately need no navigation;
the point of the measurement is that today there is no way to tell which.

## Repository root

| Measure | Value |
|---|---|
| Tracked files in root | 21 |
| Committed build/scratch artifacts | 4 |

| File | What it is |
|---|---|
| `build_out.txt` | Captured MSBuild output from one local Release build |
| `full_tests.txt` | Captured CTest output, recording 383 tests |
| `issue-body.md` | Two-line scratch body for a one-off issue |
| `Testing/Temporary/LastTest.log` | CTest log from one local run |

The scan covers **all tracked files, not only the repository root**. Its first
version matched root-level paths only and reported three, missing
`Testing/Temporary/LastTest.log` — which is also the one no reader would spot by
looking at the root listing. It was committed by accident and still claims 383
tests, a number the suite passed months ago on its way to 1,207. A stale
committed test log is worse than none: it reads as evidence.

Additional untracked residue is present locally
(`cmake_test_discovery_*.json`, `imgui.ini`, `CMakeFiles/`, `tmp/`, `shots/`) but
is not in source control. Removing all of this and preventing recurrence is
[#289](https://github.com/lasrod/assurance-forge/issues/289); this baseline only
records it.

## Quality tooling

| Tool | Configured |
|---|---|
| clang-format | yes |
| Coverage (gcovr) | yes |
| clang-tidy | no |
| cppcheck | no |
| Sanitizers (ASan/UBSan) | no |
| Fuzzing | no |
| Explicit warning level / `-Werror` | **no** |

The last row is the most consequential finding in this section. The build sets
`/FS /utf-8 /MP` on MSVC and nothing warning-related on any compiler: there is no
`-Wall`, no `-Wextra`, no `/W4`, and no `-Werror` anywhere in the first-party
CMake files. **The project builds at each compiler's default warning level.**
Any claim about warning cleanliness is currently unfounded in either direction —
the warnings have not been suppressed, they have simply never been requested.

## CI

| Measure | Value |
|---|---|
| Platforms | Windows (`windows-latest`), Linux (`ubuntu-latest`), macOS (`macos-latest`) |
| Build type | Debug |
| Workflows | 8 |
| Median successful run | 12.7 min |
| Range | 10.6 – 14.4 min |

Durations are whole-workflow wall clock over the last 20 successful runs on
`main`, including queueing — not per-job CPU time. Linux and macOS use ccache;
Windows does not.

The 8 workflows are `ci`, `coverage`, `docs`, `docs-pages`, `release`,
`roadmap-create-epic`, `roadmap-request-opened`, and `roadmap-scripts-tests`.

## AI development assets

| Measure | Value |
|---|---|
| Claude agent definitions | 10 |
| Codex agent definitions | 9 |
| Roles defined in both, hand-maintained | 9 |
| Skills | 2 |

Nine roles exist as a hand-written `.md` under `.claude/agents/` and a hand-written
`.toml` under `.codex/agents/`. There is no generator and no drift check, so the
two copies of each role can diverge silently and nothing would report it.
`feature-matrix-steward` exists only for Claude. This is the duplication
[#294](https://github.com/lasrod/assurance-forge/issues/294) addresses.

## Hotspots

Size and change frequency, both measured. Neither is a defect count; they rank
where cleanup effort is most likely to pay for itself.

### Largest production files

| File | Physical lines |
|---|---|
| `libs/sacm/src/io/xmi_reader.cpp` | 2,299 |
| `src/core/audit/event_replayer.cpp` | 1,979 |
| `src/app/app_runtime_project.cpp` | 1,973 |
| `libs/sacm/src/commands/commands.cpp` | 1,923 |
| `src/sacm_adapter/document_edit.cpp` | 1,690 |
| `src/core/element_factory.cpp` | 1,600 |
| `src/app/app_runtime.cpp` | 1,271 |
| `src/app/areas/perf_overlay_area.cpp` | 1,247 |
| `src/ui/gsn/gsn_canvas.cpp` | 912 |
| `src/core/gsn_layout.cpp` | 900 |

### Most-changed production files

Commits touching each file across full history, merge commits excluded.

| File | Commits |
|---|---|
| `src/app/app_runtime.cpp` | 62 |
| `src/app/app_runtime.h` | 48 |
| `src/app/app_runtime_project.cpp` | 43 |
| `src/app/app_runtime_state.h` | 34 |
| `src/app/app_runtime_frame.cpp` | 31 |
| `src/ui/gsn/gsn_canvas.cpp` | 29 |
| `src/ui/ui_state.h` | 27 |
| `src/core/app_state.cpp` | 25 |
| `src/ui/panels/element_panel.cpp` | 23 |
| `src/app/main.cpp` | 20 |

`app_runtime.*` dominates both size and churn, and `src/app/app_runtime_project.cpp`
appears near the top of both lists — the strongest single hotspot signal in the
repository, and the natural first candidate for
[#293](https://github.com/lasrod/assurance-forge/issues/293).

Churn over full history is biased toward files that existed early. The repository
is four months old, so the bias is mild, but it is real.

## Unavailable measurements

Listed with the reason and what would produce them. These are gaps in the
baseline, not zeros.

| Measurement | Why unavailable | How to obtain |
|---|---|---|
| Compiler warning counts | No warning level is configured, and counting requires a clean build per compiler | Set warning flags, then `cmake --build --preset release` and count |
| Static-analysis findings | No clang-tidy or cppcheck configuration exists | Add a configuration, run in report mode first ([#293](https://github.com/lasrod/assurance-forge/issues/293)) |
| Cyclomatic / cognitive complexity | No tool configured; not derivable from a source scan | Add lizard, clang-tidy `readability-function-cognitive-complexity`, or equivalent |
| Duplicated-code volume | No tool configured | Add a clone detector |
| Coverage per subsystem | The Coverage workflow defines 4 fixed scopes, not per-subsystem reports | Extend `.github/workflows/coverage.yml` with per-directory gcovr filters ([#292](https://github.com/lasrod/assurance-forge/issues/292)) |
| Per-job CI durations | `gh run list` reports whole-workflow wall clock only | Query the jobs API per run |
| Test execution time distribution | Requires a timed full run on a controlled machine | `ctest --test-dir build -C Debug --output-junit results.xml` |
| SACM matrix completeness against the specification | Requires normative-source review, not a source scan | [#295](https://github.com/lasrod/assurance-forge/issues/295) |

## Recommended indicators to track

Not targets. These are the measures worth watching over the programme, chosen
because each has a defensible interpretation and a baseline value above.

| Indicator | Baseline | Why this one |
|---|---|---|
| Layer-gate exceptions | 2 | Directly measures architectural erosion; should trend to 0 |
| Committed build/scratch artifacts | 4 | Cheap, unambiguous hygiene signal; should stay 0 once cleared |
| Unreachable documentation pages | 30 | Measures whether documentation is navigable, not merely present |
| Broken internal links | 0 | Currently clean; worth a gate to keep it so |
| Hand-duplicated agent roles | 9 | Should trend to 0 as generation replaces copies |
| Coverage, SACM library scope (lines) | 84.6% | The scope that conformance claims rest on; ratchet from here |
| Coverage, logic scope (lines) | 71.7% | The honest application-logic figure |
| Non-`verified` SACM rows | 1 | Direct measure of the #295 evidence gap |
| `supported` capability rows citing no test | 0 | Already enforced; keep it enforced |
| Median CI wall clock | 12.7 min | Guards against the feedback loop degrading unnoticed |

Deliberately **not** proposed as indicators: total line count, total test count,
and file count. Each can be moved without improving anything.

## Findings routed to other workstreams

| Finding | Workstream |
|---|---|
| 4 committed build/scratch artifacts, one of them nested under `Testing/` | [#289](https://github.com/lasrod/assurance-forge/issues/289) |
| 30 documentation pages reachable from nothing, including `ARCHITECTURE.md` and `RELEASING.md` | [#290](https://github.com/lasrod/assurance-forge/issues/290) |
| No CI link check, so today's zero broken links is unprotected | [#290](https://github.com/lasrod/assurance-forge/issues/290) |
| `sacm/` include prefix served by two subsystems | [#291](https://github.com/lasrod/assurance-forge/issues/291) |
| Layer gate checks includes but not CMake target dependencies | [#291](https://github.com/lasrod/assurance-forge/issues/291) |
| `src/core` is a third of production code despite the "keep core small" rule | [#291](https://github.com/lasrod/assurance-forge/issues/291) |
| Conformance evidence concentrated in 13 library test files with little cross-checking | [#292](https://github.com/lasrod/assurance-forge/issues/292), [#295](https://github.com/lasrod/assurance-forge/issues/295) |
| No per-subsystem coverage reporting | [#292](https://github.com/lasrod/assurance-forge/issues/292) |
| No sanitizer or fuzzing job | [#292](https://github.com/lasrod/assurance-forge/issues/292) |
| No warning level configured on any compiler | [#293](https://github.com/lasrod/assurance-forge/issues/293) |
| No static analysis configured | [#293](https://github.com/lasrod/assurance-forge/issues/293) |
| `app_runtime.*` leads both size and churn | [#293](https://github.com/lasrod/assurance-forge/issues/293) |
| 9 agent roles hand-duplicated across two platform directories with no drift check | [#294](https://github.com/lasrod/assurance-forge/issues/294) |
| `SACM23-LIB-002` is the sole non-`verified` row | [#295](https://github.com/lasrod/assurance-forge/issues/295) |
| Matrix completeness against the normative specification is unmeasured | [#295](https://github.com/lasrod/assurance-forge/issues/295) |

## Reproducing

Prerequisites: Python 3.10+, `git`, and a checkout with submodules initialized.
Optional: the GitHub CLI (`gh`), authenticated, for CI durations and coverage;
a configured build tree for the CTest count. Measurements that need an absent
prerequisite are reported as unavailable rather than silently skipped.

```bash
git submodule update --init --recursive
python tools/quality/collect_baseline.py
```

This writes [repository-baseline.json](repository-baseline.json) and prints the
markdown tables embedded above. To confirm the committed JSON still matches the
tree:

```bash
python tools/quality/collect_baseline.py --check
```

`--check` ignores CI durations, coverage, the CTest count, and the working-tree
dirty count, since none of those is a property of the committed tree.

**`--check` is deliberately not wired into CI.** A baseline describes one commit;
almost any subsequent change makes it stale, and that is correct behaviour rather
than a failure. Gating CI on it would force every unrelated pull request to
regenerate the snapshot, which would both add noise and destroy the fixed
reference point the baseline exists to provide. Use `--check` when you intend to
refresh the baseline, not to police it.
